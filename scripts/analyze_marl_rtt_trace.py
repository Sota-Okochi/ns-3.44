#!/usr/bin/env python3
"""Decompose ICMP RTT from passive MARL traces; missing stages remain blank."""
import argparse
import csv
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ap',type=int,default=1)
    p.add_argument('--sequence-start',type=int,default=30)
    p.add_argument('--count',type=int,default=10)
    p.add_argument('--monitor-node',type=int,default=5)
    p.add_argument('--server-node',type=int,default=42)
    p.add_argument('--router-node',type=int,default=38)
    p.add_argument('--ap-node',type=int,default=2)
    a=p.parse_args()
    with a.trace.open() as f: rows=list(csv.DictReader(f))
    output=[]
    for seq in range(a.sequence_start,a.sequence_start+a.count):
        rr=[r for r in rows if int(r['ap'])==a.ap and int(r['sequence'])==seq]
        def t(node,event,typ=0):
            values=[float(r['time']) for r in rr if int(r['node'])==node and
                    r['event']==event and int(r['icmp_type'])==typ]
            return min(values) if values else None
        def ms(x,y):return (y-x)*1000 if x is not None and y is not None else None
        sent=t(a.monitor_node,'ip_Tx',8);server=t(a.server_node,'ip_Rx',8)
        rout=t(a.router_node,'ip_Tx');mac=t(a.router_node,'p2p_MacTx')
        phy=t(a.router_node,'p2p_PhyTxBegin');ap=t(a.ap_node,'ip_Rx')
        received=t(a.monitor_node,'ip_Rx')
        output.append(dict(ap=a.ap,sequence=seq,send_s=sent,receive_s=received,
            rtt_ms=ms(sent,received),outbound_ms=ms(sent,server),server_to_router_ms=ms(server,rout),
            router_ip_to_device_ms=ms(rout,mac),p2p_device_wait_ms=ms(mac,phy),
            p2p_link_ms=ms(phy,ap),ap_to_monitor_ms=ms(ap,received)))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(output[0]),lineterminator='\n')
        writer.writeheader();writer.writerows(output)
    print(a.output.read_text())


if __name__=='__main__': main()
