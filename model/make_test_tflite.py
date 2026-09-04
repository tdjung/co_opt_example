#!/usr/bin/env python3
"""Build a DS-CNN Small (same topology as ML-zoo) with random-initialised
weights, fully int8-quantise it and save a .tflite -- used to validate
gen_weights.py --tflite and the CMSIS-NN implementation against the TFLite
interpreter when the real ML-zoo file is not available."""
import sys, numpy as np, tensorflow as tf
out = sys.argv[1] if len(sys.argv) > 1 else "test_ds_cnn_s.tflite"
tf.keras.utils.set_random_seed(7)
inp = tf.keras.Input(shape=(490,), name="input")
x = tf.keras.layers.Reshape((49, 10, 1))(inp)
x = tf.keras.layers.Conv2D(64, (10, 4), strides=(2, 2), padding="same")(x)
x = tf.keras.layers.BatchNormalization()(x); x = tf.keras.layers.ReLU()(x)
for _ in range(4):
    x = tf.keras.layers.DepthwiseConv2D((3, 3), strides=(1, 1), padding="same")(x)
    x = tf.keras.layers.BatchNormalization()(x); x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.Conv2D(64, (1, 1))(x)
    x = tf.keras.layers.BatchNormalization()(x); x = tf.keras.layers.ReLU()(x)
x = tf.keras.layers.AveragePooling2D(pool_size=(25, 5), strides=1)(x)
x = tf.keras.layers.Reshape((64,))(x)
o = tf.keras.layers.Dense(12, activation="softmax")(x)
m = tf.keras.Model(inp, o)
rng = np.random.default_rng(0)
def rep():
    for _ in range(50):
        yield [(rng.standard_normal((1, 490)) * 10 - 20).astype(np.float32)]
conv = tf.lite.TFLiteConverter.from_keras_model(m)
conv.optimizations = [tf.lite.Optimize.DEFAULT]
conv.representative_dataset = rep
conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
conv.inference_input_type = tf.int8
conv.inference_output_type = tf.int8
open(out, "wb").write(conv.convert())
print("wrote", out)
