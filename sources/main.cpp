#include <iostream>
#include <date/date.h>
#include <fplog_exceptions.h>
#include <sprot.h>
#include <utils.h>
#include <udp_transport.h>
#include <gtest/gtest.h>
#include <string.h>
#include <l1_transport.h>
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
    sprot::Udp_Transport l0;
    sprot::L1_transport l1(&l0);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int res = RUN_ALL_TESTS();

    return res;
}
