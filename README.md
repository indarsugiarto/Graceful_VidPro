# Graceful_VidPro

This program implements distributed image processing on SpiNNaker.
Currently implemented operation:
- Gaussian filtering (smoothing)
- Histogram equalization (sharpening)
- Sobel and Laplace edge detection

## Media for experiments
https://github.com/indarsugiarto/Graceful_VidPro_Media.git

## Branches log
- up_to_gray_fwd: working with gray pixels forwarding
- histo: adding histogram concept
- wProfiler: properly handling the profiler program
- wFilt: properly implement filtering & sharpening with additional FPGA output
