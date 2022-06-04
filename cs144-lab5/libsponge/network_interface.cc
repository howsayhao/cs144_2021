#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // 遍历map缓存找是否有对应的mac地址
    auto it = _map_cache.begin();
    for (; it != _map_cache.end(); it++) {
        if (it->_map_ip_address == next_hop_ip) {
            break;
        }
    }    

    // map中有对应的mac地址，直接发
    if (it != _map_cache.end()) {
        EthernetFrame ipv4_frame;
        // cerr << "开始" << endl;
        ipv4_frame.payload() = dgram.serialize();
        // cerr << "结束" << endl;
        ipv4_frame.header().type = EthernetHeader::TYPE_IPv4;
        ipv4_frame.header().dst = it->_map_ethernet_address;
        ipv4_frame.header().src = _ethernet_address;
        _frames_out.push(ipv4_frame);
    }

    // 没有找到，需要发送arp请求
    else {
        // 在此之前要先看看是不是已经发过了
        auto li = _ip_queue.begin();
        for (; li != _ip_queue.end(); li ++) {
            if (li->first.ipv4_numeric() == next_hop_ip) {
                    return ;
            }
        }

        // 构建request的rsp载荷，广播发送
        ARPMessage arp_msg;
        arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
        arp_msg.sender_ethernet_address = _ethernet_address;
        arp_msg.sender_ip_address = _ip_address.ipv4_numeric();
        arp_msg.target_ethernet_address = {0, 0, 0, 0, 0, 0};
        arp_msg.target_ip_address = next_hop_ip;

        // 构建对应以太网帧
        EthernetFrame arp_frame;
        arp_frame.payload() = arp_msg.serialize();
        arp_frame.header().type = EthernetHeader::TYPE_ARP;
        arp_frame.header().dst = ETHERNET_BROADCAST;
        arp_frame.header().src = _ethernet_address;

        // 广播发送arp以太网帧
        _frames_out.push(arp_frame);

        // 将对应ip推入队列，等待回应
        _ip_queue[next_hop] = dgram;  
        _ip_life[next_hop] = 5000;
                                                /* C++是真的没玩明白，这已经是第二处我觉得逻辑没问题的地方出错了
                                                    这里如果给_ipv4_life赋值的话，对_ipv4_datagram的赋值就会不完整
                                                    貌似刚好缺了5个bytes,差不多这个量级,注意,这个量级应该不是5000的量级
                                                    因为后面我改为开始初始化0,然后这里赋值,一样不行                   */
                                                /* 讲到这里还有一个奇怪的点,这个ipv4_queue的成员需要初始化,但那个mapping的就不需要
                                                    所以mapping那里就不会出问题,恩,,,也不一定,因为mapping是队列类不是mapping类
                                                    靠,好烦    */
                                                /* 5/26更新
                                                    我把数据结构给改了，map的第二元素好像不能用自己的新定义结构体
                                                    我也不懂为什么阿，反正有问题
                                                    虽然按前面的可以糊弄地通过lab5的test,但到lab6还是会爆出发送datagram数据不对的情况
                                                    其实就是我按原来的数据结构存入map的datagram是有问题的
                                                    现在分成两个dequeue的数据结构，就没什么问题了，但也就和@Mr小明的也差不离了... */
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 如果目标既不是该主机mac地址也不是广播，无视
    if (not(    _ethernet_address_equal(frame.header().dst, _ethernet_address) \
             || _ethernet_address_equal(frame.header().dst, ETHERNET_BROADCAST))) 
    {
        return {};
    }
    
    // 如果目标是该主机mac地址
    if(_ethernet_address_equal(frame.header().dst, _ethernet_address)) {
        // 如果是 arp 帧，说明是返回的 arp 帧
        if (frame.header().type == EthernetHeader::TYPE_ARP) {
            ARPMessage recv_arp;
            // 如果能正常解码为arp格式，且是一个reply帧
            if (recv_arp.parse(frame.payload()) == ParseResult::NoError && recv_arp.opcode == ARPMessage::OPCODE_REPLY) {
                auto it = _ip_queue.begin();
                // 寻找待定queue队列中是否有对应的等待ip
                for (; it != _ip_queue.end(); ) {
                    // 如果找到了，那就pop出队列，并在mapping缓存增加新的mapping
                    if (it->first.ipv4_numeric() == recv_arp.sender_ip_address) {
                        mapping new_map;
                        // 增加新的mapping缓存
                        new_map._map_ip_address = recv_arp.sender_ip_address;
                        new_map._map_ethernet_address = recv_arp.sender_ethernet_address;
                        new_map._map_left_time = 30000;
                        _map_cache.push_back(new_map);
                        InternetDatagram send_ipv4_datagram = it->second;
                        Address send_ipv4_address = it->first;
                        // pop出队列
                        it = _ip_queue.erase(it);
                        // 调用send发送ipv4数据报
                        send_datagram(send_ipv4_datagram, send_ipv4_address);
                        break;
                    } else {
                        it ++;
                    }
                }
                // 如果没有找到等待ip,说明返回的reply arp非法
                if (it == _ip_queue.end()) {
                    return {}; // 返回不管
                } 
            } else { // 否则就返回不管
                return {};
            }
        } else { // 否则，是对方发送的 ipv4 帧
            InternetDatagram recv_ipv4;
            // 如果可以正常解码为ipv4格式，ok，收到，done
            if (recv_ipv4.parse(frame.payload()) == ParseResult::NoError) {
                return recv_ipv4;
            } else { // 否则返回不管
                return {};
            }
        }
    }

    // 如果是广播的帧
    else {
        ARPMessage recv_arp;
        // 广播的帧如果是ipv4，则不管
        if (frame.header().type == EthernetHeader::TYPE_IPv4) {
            return {};
        } else // 广播的帧一定是 可解码 请求 arp
        if (recv_arp.parse(frame.payload()) == ParseResult::NoError && recv_arp.opcode == ARPMessage::OPCODE_REQUEST) {
            auto it = _ip_queue.begin();
            // 不管是不是目标主机，都可以借机清一下对应的等待ip
            for (; it != _ip_queue.end(); it++) {
                // 如果找到了，那就pop出队列，且因为这个说明没有mapping，要添上
                if (it->first.ipv4_numeric() == recv_arp.sender_ip_address) {
                    mapping new_map;
                    // 增加新的mapping缓存
                    new_map._map_ip_address = recv_arp.sender_ip_address;
                    new_map._map_ethernet_address = recv_arp.sender_ethernet_address;
                    new_map._map_left_time = 30000;
                    _map_cache.push_back(new_map);
                    // pop出队列
                    _ip_queue.erase(it);
                    // 调用send发送ipv4数据报
                    send_datagram(it->second, it->first);
                    break;
                }
            }
            // 如果没有等待该ip的队列元素，
            // 考虑如果没有相应的mapping,要添上
            auto li = _map_cache.begin();
            if (it == _ip_queue.end()) {
                for (; li != _map_cache.end(); li ++) {
                    if (li->_map_ip_address == recv_arp.sender_ip_address) {
                        break;
                    }
                }
            } 
            // 没有相应mapping
            if (li == _map_cache.end()) {
                mapping new_map;
                // 增加新的mapping缓存
                new_map._map_ip_address = recv_arp.sender_ip_address;
                new_map._map_ethernet_address = recv_arp.sender_ethernet_address;
                new_map._map_left_time = 30000;
                _map_cache.push_back(new_map);
            }
            // 最后如果请求的ip是自己的ip，那么
            // 还要返回reply的arp帧，来告诉发起者自己的mac地址
            if (recv_arp.target_ip_address == _ip_address.ipv4_numeric()) {
                    // 构建request的rsp载荷，广播发送
                    ARPMessage arp_msg;
                    arp_msg.opcode = ARPMessage::OPCODE_REPLY;  
                    arp_msg.sender_ethernet_address = _ethernet_address;
                    arp_msg.sender_ip_address = _ip_address.ipv4_numeric();
                    arp_msg.target_ethernet_address = recv_arp.sender_ethernet_address;
                    arp_msg.target_ip_address = recv_arp.sender_ip_address;

                    // 构建对应以太网帧
                    EthernetFrame arp_frame;
                    arp_frame.payload() = arp_msg.serialize();
                    arp_frame.header().type = EthernetHeader::TYPE_ARP;
                    arp_frame.header().dst = recv_arp.sender_ethernet_address;
                    arp_frame.header().src = _ethernet_address;

                    // 发送arp以太网帧
                    _frames_out.push(arp_frame);
            }
        } else { // 否则不管
            return {};
        }
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    auto it = _map_cache.begin();
    for (; it != _map_cache.end(); ) {
        it->_map_left_time = (it->_map_left_time>ms_since_last_tick)?it->_map_left_time-ms_since_last_tick:0;
        if (it->_map_left_time == 0) {
            it = _map_cache.erase(it); /* 很神奇，后面li那里用++就可以，这里用就会报错
                                          我不明白阿这个，C++好玄乎...             */
                                       /* 5/26更新
                                          这个我大概知道了，因为之前后面那里用的是map数据结构，而这里是dequeue
                                          不支持对应操作后it的移动也属正常   */
        } else {
            it ++;
        }
    }
    auto li = _ip_life.begin();
    for (; li != _ip_life.end(); ) {
        li->second = (li->second>ms_since_last_tick)?li->second-ms_since_last_tick:0;
        if (li->second == 0) {
            auto lt = _ip_queue.begin();
            for (; lt != _ip_queue.end(); ) {
                if (lt->first.ipv4_numeric() == li->first.ipv4_numeric()) {
                    lt = _ip_queue.erase(lt);
                } else {
                    lt ++;
                }
            }
            li = _ip_life.erase(li);
        } else {
            li ++;
        }
    }
}



bool NetworkInterface::_ethernet_address_equal(EthernetAddress addr1, EthernetAddress addr2) {
    auto i = 0;
    for (; i < 6; i++) {
        if(addr1[i] != addr2[i]) {
            return false;
        }
    }
    return true;
}