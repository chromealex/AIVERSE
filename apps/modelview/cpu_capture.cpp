#include "HeadlessModelCapture.h"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv) {
    if(argc==5 && std::strcmp(argv[1],"--backend")==0)
        return aiverse::modelview::captureHeadlessModel(argv[2],argv[3],argv[4]);
    if(argc!=3){std::fprintf(stderr,"usage: aiverse_cpu_capture [--backend NAME] MODEL OUTPUT.png\n");return 2;}
    return aiverse::modelview::captureHeadlessModel("cpu",argv[1],argv[2]);
}
