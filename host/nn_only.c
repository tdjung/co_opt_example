/* nn_only: run the DS-CNN on raw int8 feature vectors (490 bytes each,
 * concatenated in a file) and print logits/probs per vector. Used to
 * cross-check CMSIS-NN against the TFLite interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include "ds_cnn.h"
#include "hal.h"
extern void hal_host_parse_args(int, char **);
static const char *g_feat;
int app_main(void)
{
    FILE *f = fopen(g_feat, "rb");
    if (!f) { perror(g_feat); return 2; }
    int8_t feat[KWS_IN_H * KWS_IN_W], logits[KWS_N_CLASSES], probs[KWS_N_CLASSES];
    ds_cnn_init();
    int n = 0;
    while (fread(feat, 1, sizeof feat, f) == sizeof feat) {
        int best = ds_cnn_run(feat, logits, probs);
        printf("vec=%d best=%d logits=", n++, best);
        for (int i = 0; i < KWS_N_CLASSES; i++) printf("%s%d", i ? "," : "", logits[i]);
        printf(" probs=");
        for (int i = 0; i < KWS_N_CLASSES; i++) printf("%s%d", i ? "," : "", probs[i]);
        printf(" cksum=");
        const uint32_t *ck = ds_cnn_layer_checksums();
        for (int i = 0; i < DS_CNN_N_LAYERS; i++) printf("%s%08x", i ? "," : "", (unsigned)ck[i]);
        printf("\n");
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: nn_only features.bin [--mac]\n"); return 2; }
    g_feat = argv[1];
    if (argc > 2) hal_host_parse_args(argc - 1, argv + 1);   /* --mac */
    return app_main();
}
