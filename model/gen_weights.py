#!/usr/bin/env python3
"""Generate C weight/quantisation sources for the DS-CNN Small KWS model.

Two modes:
  random : deterministic pseudo-random int8 weights (seeded). Functionally
           meaningless labels, but identical memory footprint / cycle behaviour
           to the real model. Use until the ML-zoo tflite is available.
  tflite : extract weights + quantisation parameters from
           ds_cnn_s_quantized.tflite (Arm ML-zoo). Requires `pip install tflite`.

Output: model/generated/kws_weights.c and kws_weights.h

Layout conventions (CMSIS-NN, NHWC activations):
  conv   weights : [out_ch][kh][kw][in_ch]   (OHWI)
  dwconv weights : [1][kh][kw][ch]
  fc     weights : [out][in]
  per-channel requant multiplier/shift for conv/dw/pw, per-tensor for fc.
"""
import argparse
import os
import sys
import numpy as np

# ---- fixed DS-CNN Small topology (ML-zoo model_size_info 5 64 10 4 2 2 64 3 3 1 1 x4)
IN_H, IN_W, IN_C = 49, 10, 1
CH = 64
CONV1_KH, CONV1_KW, CONV1_SH, CONV1_SW = 10, 4, 2, 2
N_DS = 4
DW_K = 3
N_CLASSES = 12
OUT_H = -(-IN_H // CONV1_SH)   # ceil -> 25
OUT_W = -(-IN_W // CONV1_SW)   # ceil -> 5
LABELS = ["silence", "unknown", "yes", "no", "up", "down", "left", "right",
          "on", "off", "stop", "go"]


def same_pad(in_size, k, s):
    out = -(-in_size // s)
    total = max((out - 1) * s + k - in_size, 0)
    return total // 2, total - total // 2   # (before, after)


def c_array(name, ctype, arr, per_line=16, section=""):
    flat = np.asarray(arr).flatten()
    lines = [f"const {ctype} {name}[{flat.size}] {section} = {{"]
    for i in range(0, flat.size, per_line):
        chunk = ", ".join(str(int(v)) for v in flat[i:i + per_line])
        lines.append("    " + chunk + ",")
    lines.append("};")
    return "\n".join(lines)


class Layer:
    def __init__(self, name, kind, w, b, mult, shift, in_zp, out_zp, in_scale=None, out_scale=None):
        self.name, self.kind, self.w, self.b = name, kind, w, b
        self.mult, self.shift = mult, shift
        self.in_zp, self.out_zp = in_zp, out_zp
        self.in_scale, self.out_scale = in_scale, out_scale


def gen_random(seed):
    rng = np.random.default_rng(seed)

    def w8(shape):
        return rng.integers(-127, 128, size=shape, dtype=np.int8)

    def b32(n, spread):
        return rng.integers(-spread, spread, size=n, dtype=np.int32)

    def mult(n):
        return rng.integers(0x40000000, 0x7FFFFFFF, size=n, dtype=np.int32)

    layers = []
    # All activation tensors use zero-point -128 (post-ReLU) except the input.
    in_zp = 0
    act_zp = -128
    layers.append(Layer("conv1", "conv", w8((CH, CONV1_KH, CONV1_KW, IN_C)), b32(CH, 20000),
                        mult(CH), np.full(CH, -9, np.int32), in_zp, act_zp))
    for i in range(N_DS):
        layers.append(Layer(f"dw{i+1}", "dw", w8((1, DW_K, DW_K, CH)), b32(CH, 8000),
                            mult(CH), np.full(CH, -7, np.int32), act_zp, act_zp))
        layers.append(Layer(f"pw{i+1}", "conv", w8((CH, 1, 1, CH)), b32(CH, 20000),
                            mult(CH), np.full(CH, -9, np.int32), act_zp, act_zp))
    layers.append(Layer("fc", "fc", w8((N_CLASSES, CH)), b32(N_CLASSES, 5000),
                        mult(1), np.array([-9], np.int32), act_zp, -20))
    model = {
        "layers": layers,
        "input_scale": 0.5, "input_zp": in_zp,
        "avgpool_zp": act_zp,
        "softmax_mult": 1717986918, "softmax_shift": 23, "softmax_diff_min": -248,
        "source": f"random(seed={seed})",
    }
    return model


def quantize_multiplier(scale):
    """TFLite QuantizeMultiplier: scale -> (int32 multiplier, int shift) with mult in [2^30, 2^31)."""
    if scale == 0.0:
        return 0, 0
    q, shift = np.frexp(scale)
    q_fixed = int(round(q * (1 << 31)))
    if q_fixed == (1 << 31):
        q_fixed //= 2
        shift += 1
    if shift < -31:
        return 0, 0
    return q_fixed, int(shift)


def gen_tflite(path):
    try:
        import tflite
    except ImportError:
        sys.exit("pip install tflite  (schema reader) is required for --tflite")
    with open(path, "rb") as f:
        buf = f.read()
    m = tflite.Model.GetRootAsModel(buf, 0)
    sg = m.Subgraphs(0)

    def tensor(i):
        return sg.Tensors(i)

    def tdata(t):
        b = m.Buffers(t.Buffer())
        return b.DataAsNumpy() if b.DataLength() else None

    def tq(t):
        q = t.Quantization()
        s = q.ScaleAsNumpy() if q.ScaleLength() else None
        z = q.ZeroPointAsNumpy() if q.ZeroPointLength() else None
        return s, z

    layers = []
    in_t = tensor(sg.Inputs(0))
    in_scale, in_zp = tq(in_t)
    input_scale, input_zp = float(in_scale[0]), int(in_zp[0])
    softmax = None
    pool_zp = None
    conv_i, ds_i = 0, 0
    for oi in range(sg.OperatorsLength()):
        op = sg.Operators(oi)
        code = m.OperatorCodes(op.OpcodeIndex()).BuiltinCode()
        ins = [op.Inputs(k) for k in range(op.InputsLength())]
        out = tensor(op.Outputs(0))
        os_, oz = tq(out)
        if code in (tflite.BuiltinOperator.CONV_2D, tflite.BuiltinOperator.DEPTHWISE_CONV_2D):
            it, wt = tensor(ins[0]), tensor(ins[1])
            isc, izp = tq(it)
            wsc, _ = tq(wt)
            w = tdata(wt).view(np.int8).reshape([wt.Shape(k) for k in range(wt.ShapeLength())])
            b = tdata(tensor(ins[2])).view(np.int32) if ins[2] >= 0 else np.zeros(w.shape[0] if code == tflite.BuiltinOperator.CONV_2D else w.shape[-1], np.int32)
            eff = (float(isc[0]) * wsc.astype(np.float64)) / float(os_[0])
            mults, shifts = zip(*[quantize_multiplier(float(e)) for e in eff])
            if code == tflite.BuiltinOperator.CONV_2D:
                name = "conv1" if conv_i == 0 else f"pw{conv_i}"
                conv_i += 1
                layers.append(Layer(name, "conv", w, b, np.array(mults, np.int32), np.array(shifts, np.int32),
                                    int(izp[0]), int(oz[0]), float(isc[0]), float(os_[0])))
            else:
                ds_i += 1
                layers.append(Layer(f"dw{ds_i}", "dw", w, b, np.array(mults, np.int32), np.array(shifts, np.int32),
                                    int(izp[0]), int(oz[0]), float(isc[0]), float(os_[0])))
        elif code == tflite.BuiltinOperator.AVERAGE_POOL_2D:
            pool_zp = int(oz[0])
        elif code == tflite.BuiltinOperator.FULLY_CONNECTED:
            it, wt = tensor(ins[0]), tensor(ins[1])
            isc, izp = tq(it)
            wsc, _ = tq(wt)
            w = tdata(wt).view(np.int8).reshape(wt.Shape(0), wt.Shape(1))
            b = tdata(tensor(ins[2])).view(np.int32) if ins[2] >= 0 else np.zeros(w.shape[0], np.int32)
            # per-tensor (1 scale) or per-channel (out_ch scales) filter quantisation
            eff = (float(isc[0]) * wsc.astype(np.float64)) / float(os_[0])
            mults, shifts = zip(*[quantize_multiplier(float(e)) for e in eff])
            layers.append(Layer("fc", "fc", w, b, np.array(mults, np.int32), np.array(shifts, np.int32),
                                int(izp[0]), int(oz[0]), float(isc[0]), float(os_[0])))
        elif code == tflite.BuiltinOperator.SOFTMAX:
            it = tensor(ins[0])
            isc, _ = tq(it)
            # TFLM softmax s8 param derivation
            beta = 1.0
            input_beta_real_multiplier = min(beta * float(isc[0]) * (1 << (31 - 5)), (1 << 31) - 1.0)
            mu, sh = quantize_multiplier(input_beta_real_multiplier)
            # diff_min = -CalculateInputRadius(kScaledDiffIntegerBits=5, shift)
            input_integer_bits = 5
            max_input_rescaled = (1.0 * ((1 << input_integer_bits) - 1) *
                                  (1 << (31 - input_integer_bits)) / (1 << sh))
            diff_min = -int(np.floor(max_input_rescaled))
            softmax = (mu, sh, diff_min)
    assert softmax is not None and pool_zp is not None
    return {
        "layers": layers, "input_scale": input_scale, "input_zp": input_zp,
        "avgpool_zp": pool_zp,
        "softmax_mult": softmax[0], "softmax_shift": softmax[1], "softmax_diff_min": softmax[2],
        "source": os.path.basename(path),
    }


def emit(model, outdir):
    os.makedirs(outdir, exist_ok=True)
    layers = model["layers"]
    h = ["/* AUTO-GENERATED by model/gen_weights.py -- do not edit. Source: %s */" % model["source"],
         "#ifndef KWS_WEIGHTS_H", "#define KWS_WEIGHTS_H", "#include <stdint.h>",
         "#include \"placement.h\"", "",
         f"#define KWS_IN_H {IN_H}", f"#define KWS_IN_W {IN_W}", f"#define KWS_IN_C {IN_C}",
         f"#define KWS_CH {CH}", f"#define KWS_OUT_H {OUT_H}", f"#define KWS_OUT_W {OUT_W}",
         f"#define KWS_N_DS {N_DS}", f"#define KWS_N_CLASSES {N_CLASSES}",
         f"#define KWS_CONV1_KH {CONV1_KH}", f"#define KWS_CONV1_KW {CONV1_KW}",
         f"#define KWS_CONV1_SH {CONV1_SH}", f"#define KWS_CONV1_SW {CONV1_SW}",
         f"#define KWS_DW_K {DW_K}",
         f"#define KWS_INPUT_SCALE {model['input_scale']!r}f", f"#define KWS_INPUT_ZP {model['input_zp']}",
         f"#define KWS_AVGPOOL_ZP {model['avgpool_zp']}",
         f"#define KWS_SOFTMAX_MULT {model['softmax_mult']}", f"#define KWS_SOFTMAX_SHIFT {model['softmax_shift']}",
         f"#define KWS_SOFTMAX_DIFF_MIN {model['softmax_diff_min']}", ""]
    ph_t, ph_b = same_pad(IN_H, CONV1_KH, CONV1_SH)
    pw_l, pw_r = same_pad(IN_W, CONV1_KW, CONV1_SW)
    h += [f"#define KWS_CONV1_PAD_H {ph_t}", f"#define KWS_CONV1_PAD_W {pw_l}",
          f"#define KWS_DW_PAD {DW_K // 2}", ""]
    c = ["/* AUTO-GENERATED by model/gen_weights.py -- do not edit. Source: %s */" % model["source"],
         "#include \"kws_weights.h\"", ""]
    sizes = 0
    for L in layers:
        h.append(f"/* {L.name}: {L.kind}, in_zp={L.in_zp}, out_zp={L.out_zp} */")
        h.append(f"#define {L.name.upper()}_IN_ZP  ({L.in_zp})")
        h.append(f"#define {L.name.upper()}_OUT_ZP ({L.out_zp})")
        h.append(f"extern const int8_t  {L.name}_w[{L.w.size}];")
        h.append(f"extern const int32_t {L.name}_b[{L.b.size}];")
        h.append(f"extern const int32_t {L.name}_mult[{L.mult.size}];")
        h.append(f"extern const int32_t {L.name}_shift[{L.shift.size}];")
        h.append("")
        sec = f"SECTION_WEIGHTS_{L.name.upper()}"
        c.append(c_array(f"{L.name}_w", "int8_t", L.w, section=sec))
        c.append(c_array(f"{L.name}_b", "int32_t", L.b, 8, section=sec))
        c.append(c_array(f"{L.name}_mult", "int32_t", L.mult, 8, section=sec))
        c.append(c_array(f"{L.name}_shift", "int32_t", L.shift, 8, section=sec))
        c.append("")
        sizes += L.w.size + 4 * (L.b.size + L.mult.size + L.shift.size)
    h.append("static const char *const KWS_LABELS[KWS_N_CLASSES] = {" +
             ", ".join(f'"{s}"' for s in LABELS) + "};")
    fc = [L for L in layers if L.kind == "fc"][0]
    h.append(f"#define KWS_FC_PER_CHANNEL {1 if fc.mult.size > 1 else 0}")
    h.append(f"#define KWS_WEIGHTS_TOTAL_BYTES {sizes}")
    h.append("#endif")
    with open(os.path.join(outdir, "kws_weights.h"), "w") as f:
        f.write("\n".join(h) + "\n")
    with open(os.path.join(outdir, "kws_weights.c"), "w") as f:
        f.write("\n".join(c) + "\n")
    print(f"wrote {outdir}/kws_weights.[ch]: {len(layers)} layers, {sizes} bytes of weights+params")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--tflite", help="path to ds_cnn_s_quantized.tflite")
    ap.add_argument("--seed", type=int, default=20260904)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "generated"))
    a = ap.parse_args()
    model = gen_tflite(a.tflite) if a.tflite else gen_random(a.seed)
    emit(model, a.out)
