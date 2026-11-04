#define HDA_RATE      48000
#define HDA_CHANNELS  2
#define HDA_FRAME     (HDA_CHANNELS * 2)

int  hda_init(void);
void hda_poll(void);
