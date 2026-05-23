#include "NetSim.h"

int main(int argc, char *argv[]){

    ns3::NetSim sim;
    sim.Init(argc, argv);
    sim.RunSim();
    return 0;
}
