#include "HeadlessModelCapture.h"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"usage: aiverse_cpu_capture MODEL OUTPUT.png\n");return 2;}
    return aiverse::modelview::captureHeadlessModel("cpu",argv[1],argv[2]);
}
