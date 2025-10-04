#include "NetSim.h"

int main(int argc, char *argv[]){

    if(argc == 1){
        std::puts("Please input nth argument");
        return 1;
    }
    ns3::NetSim sim;
    sim.Init(argc, argv);
    sim.RunSim();
    return 0;
}
