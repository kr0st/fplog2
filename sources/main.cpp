#include <iostream>
#include <date/date.h>
#include <fplog_exceptions.h>
#include <sprot.h>
#include <utils.h>
#include <udp_transport.h>
#include <gtest/gtest.h>
#include <string.h>
#include <packet_router.h>
#include <thread>


TEST(Udp_Transport_Test, Smoke_Test)
{
    sprot::Udp_Transport t1, t2;

    sprot::Session_Manager::Params params;
    sprot::Session_Manager::Param p;

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26258";
    params.insert(p);

    t1.enable(params);

    params["port"] = "26259";

    t2.enable(params);

    unsigned char send_buf[255];
    unsigned char recv_buf[255];

    memset(send_buf, 0, sizeof(send_buf));
    memset(recv_buf, 0, sizeof(recv_buf));

    char* message = "hello world?";
    memcpy(send_buf, message, strlen(message));

    sprot::Extended_Transport_Interface::Extended_Data recepient, origin;
    recepient.push_back(static_cast<unsigned int>(0x0100007f));
    recepient.push_back(static_cast<unsigned short>(26259));

    t1.write(send_buf, strlen(message), recepient);

    size_t read_bytes = t2.read(recv_buf, sizeof(recv_buf), origin);
    EXPECT_EQ(read_bytes, strlen(message));
    EXPECT_EQ(memcmp(message, recv_buf, strlen(message)), 0);
}

TEST(L1_Transport_Test, Smoke_Test)
{
    sprot::Udp_Transport t1, t2;

    sprot::Session_Manager::Params params;
    sprot::Session_Manager::Param p;

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    t1.enable(params);

    params["port"] = "26261";

    t2.enable(params);

    unsigned char send_buf[255];
    unsigned char recv_buf[255];

    memset(send_buf, 0, sizeof(send_buf));
    memset(recv_buf, 0, sizeof(recv_buf));

    char* message = "hello world?";

    sprot::implementation::Frame frame;
    frame.details.crc = 666;
    frame.details.data_len = strlen(message);
    sprintf(frame.details.hostname, "WORKSTATION-666");
    frame.details.type = 0;
    frame.details.sequence = 999;
    frame.details.origin_ip = 0x0100007f;
    frame.details.origin_listen_port = 26261;

    memcpy(send_buf, frame.bytes, sizeof(frame.bytes));
    memcpy(send_buf + sizeof(frame.bytes), message, strlen(message));

    sprot::Extended_Transport_Interface::Extended_Data recepient, origin;
    recepient.push_back(static_cast<unsigned int>(0x0100007f));
    recepient.push_back(static_cast<unsigned short>(26260));

    sprot::Packet_Router r1(&t1);

    std::thread sender([&]
    {
        sprot::Packet_Router r2(&t2);
        r2.write(send_buf, strlen(message) + sizeof (frame.bytes), recepient);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    size_t received_bytes = r1.read(recv_buf, sizeof (recv_buf), origin);
    sprot::implementation::Frame received_frame;
    memcpy(received_frame.bytes, recv_buf, sizeof(received_frame.bytes));

    EXPECT_EQ(received_bytes, strlen(message) + sizeof (frame.bytes));
    EXPECT_EQ(memcmp(frame.bytes, received_frame.bytes, sizeof(frame.bytes)), 0);
    EXPECT_EQ(memcmp(message, recv_buf + sizeof(frame.bytes), strlen(message)), 0);

    sender.join();
    return;
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int res = RUN_ALL_TESTS();

    return res;
}
