"""sim_run.py -- platform adapter (TO BE IMPLEMENTED ON THE CLOSED NETWORK).

The harness calls run_target(...) and expects the returned dict. Everything
else in tools/harness is platform independent. Keep the interface; fill in
the body with the real simulator invocation (binary path, IR/config flags,
how the WAV is passed to the audio-source model, where callgrind/FST/log
files land).

run_host(...) is complete and used for golden generation / functional tests.
"""
import os
import shutil
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run_host(exe, wav, out_dir, mac=False, timeout=600):
    os.makedirs(out_dir, exist_ok=True)
    cmd = [exe, "--wav", wav] + (["--mac"] if mac else [])
    log = os.path.join(out_dir, "run.log")
    with open(log, "w") as f:
        rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=timeout).returncode
    return {"rc": rc, "log": log, "callgrind": None, "fst": None}


def run_target(elf, wav, platform_ir, out_dir, timeout=3600, extra_args=None):
    """Run the CM4 virtual platform.

    Args:
      elf         : firmware ELF built by firmware/target.mk
      wav         : 16 kHz mono int16 WAV fed to the audio-source model
      platform_ir : JSON IR describing the SoC (generated per tier)
      out_dir     : directory for run.log, callgrind.out, trace.fst
    Returns: {"rc": int, "log": path, "callgrind": path|None, "fst": path|None}

    TODO(closed network): replace the body below. Suggested shape:
      cmd = [SIM_BIN, "--config", platform_ir, "--elf", elf,
             "--audio-wav", wav, "--callgrind", cg, "--fst", fst, "--log", log]
    Make sure:
      - the simulation terminates on the SIM_EXIT register write (hal_exit)
      - LOG_TX bytes are captured to run.log
      - callgrind events include the extended set (see docs/HANDOFF.md)
    """
    os.makedirs(out_dir, exist_ok=True)
    sim = os.environ.get("CM4_SIM_BIN")
    if not sim or not os.path.exists(sim):
        raise NotImplementedError("sim_run.run_target: set CM4_SIM_BIN and implement the invocation")
    log = os.path.join(out_dir, "run.log")
    cg = os.path.join(out_dir, "callgrind.out")
    fst = os.path.join(out_dir, "trace.fst")
    cmd = [sim, "--config", platform_ir, "--elf", elf, "--audio-wav", wav,
           "--callgrind", cg, "--fst", fst, "--log", log] + (extra_args or [])
    rc = subprocess.run(cmd, timeout=timeout).returncode
    return {"rc": rc, "log": log, "callgrind": cg if os.path.exists(cg) else None,
            "fst": fst if os.path.exists(fst) else None}


def build_host(variant, extra_cflags="", gen=None):
    """Build host binary with knob overrides. Returns exe path."""
    host_dir = os.path.join(ROOT, "host")
    env = dict(os.environ)
    cmd = ["make", "-s", "-j8", f"VARIANT=_{variant}", f"EXTRA_CFLAGS={extra_cflags}"]
    if gen:
        cmd.append(f"GEN={gen}")
    subprocess.run(cmd, cwd=host_dir, check=True, env=env)
    return os.path.join(host_dir, f"build_{variant}", "kws_host")


def build_target(variant, ldscript, extra_cflags="", gen=None):
    """Build target ELF with knob overrides + tier linker script. Returns elf path."""
    fw = os.path.join(ROOT, "firmware")
    build = f"build_target_{variant}"
    cmd = ["make", "-s", "-f", "target.mk", "-j8", f"BUILD={build}", f"LDSCRIPT={ldscript}",
           f"EXTRA_CFLAGS={extra_cflags}"]
    if gen:
        cmd.append(f"GEN={gen}")
    subprocess.run(cmd, cwd=fw, check=True)
    return os.path.join(fw, build, "kws.elf")


def clean_build_dirs():
    for d in os.listdir(os.path.join(ROOT, "host")):
        if d.startswith("build_"):
            shutil.rmtree(os.path.join(ROOT, "host", d), ignore_errors=True)
