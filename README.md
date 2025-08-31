# MCut

A very simple, lossless & frame accurate video editor

# How does it work

MCut tries to copy as much as possible to avoid quality loss and recoding time. It furthermore tries to avoid desync between streams.

## Video

All frames between and including the first I frame and the last I/P frame of a cut will be copied. The remaining frames will be recoded with about the same parameters as the copied streams. For reducing quality loss, the bitrate of the recoded frames will be higher. All frames between the last copied frame of one cut and the first copied frame of the next cut will be recoded in one go, i.e. there will be not more I frames than needed for achieving the same GOP length as the copied streams.

## Audio

All audio frames will be copied. Since audio frames typically have different lengths than video frames, the audio may have a few milliseconds of desync and cut points may have a few miliseconds of duplicate or lost audio to achieve optimal audio synchronisation. MCut tries to keep that desync to the video as small as possible, i.e. at most half the audio frame length.

## Subtitles

Subtitles are copied if they are fully contained in the cut.

# Usage tips

For optimal usage, start MCut from command line, since found errors are logged to console.

# Disclaimer

I wrote MCut for personl usage. MCut is only tested with MPEG transport streams as input and output container format and Matroska as output container format. All other container formats may or may not work.
