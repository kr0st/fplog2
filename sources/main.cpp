﻿
#include <stdio.h>
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
#include <random>
#include <vector>
#include <protocol.h>
#include <piped_sequence.h>
#include <stdlib.h>
#include <fplog.h>
#include <queue_controller.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace fplog {

class FPLOG_API Fplog_Impl
{
    public:

        void set_test_mode(bool mode);
        void wait_until_queues_are_empty();
};

FPLOG_API extern Fplog_Impl* g_fplog_impl;
FPLOG_API extern std::vector<std::string> g_test_results_vector;

};

void print_test_vector()
{
    for (auto str : fplog::g_test_results_vector)
    {
        std::cout << str << std::endl;
    }
}

void randomize_buffer(unsigned char* buf, size_t len, std::mt19937* rng)
{
    std::uniform_int_distribution<unsigned char> range(97, 122); //ascii 'a' to 'z'
    for (size_t i = 0; i < len; ++i)
        buf[i] = range(*rng);
    buf[len - 1] = '\n';
}
static std::mt19937 g_rng1(31337);
static std::mt19937 g_rng2(31338);
static std::mt19937 g_rng3(31339);

sprot::implementation::Frame make_dummy_frame(unsigned short origin_listen_port, unsigned int origin_ip, unsigned short data_len)
{
    sprot::implementation::Frame frame;
    frame.details.crc = 666;
    frame.details.data_len = data_len;
    sprintf(frame.details.hostname, "WORKSTATION-666");
    frame.details.type = sprot::implementation::Frame_Type::Data_Frame;
    frame.details.sequence = 999;
    frame.details.origin_ip = origin_ip;
    frame.details.origin_listen_port = origin_listen_port;

    return frame;
}

unsigned short fill_buffer_with_frame_and_random_data(unsigned char* buf, unsigned short data_len, unsigned short origin_listen_port, unsigned int origin_ip, std::mt19937* rng)
{
    sprot::implementation::Frame frame(make_dummy_frame(origin_listen_port, origin_ip, data_len));
    memcpy(buf, frame.bytes, sizeof(frame.bytes));
    randomize_buffer(buf + sizeof(frame.bytes), data_len, rng);

    unsigned short crc = generic_util::gen_simple_crc16(buf + 2, static_cast<unsigned short>(sizeof(frame.bytes) + data_len) - 2);
    unsigned short *pcrc = reinterpret_cast<unsigned short*>(buf);
    *pcrc = crc;

    return (data_len + sizeof(frame.bytes));
}

unsigned long write_to_transport(unsigned int bytes_to_write, std::string file_name, std::mt19937* rng, sprot::Basic_Transport_Interface* basic = nullptr,
                                 sprot::Extended_Transport_Interface* extended = nullptr,
                                 sprot::Address& fake_origin = sprot::no_address,
                                 unsigned short real_recipient_listen_port = 0)
{
    if (bytes_to_write == 0)
        return 0;

    if ((basic == nullptr) && (extended == nullptr))
        THROW(fplog::exceptions::Transport_Missing);

    if (extended && sprot::Extended_Transport_Interface::null_data(fake_origin))
        THROW(fplog::exceptions::Incorrect_Parameter);

    if (!rng)
        THROW(fplog::exceptions::Incorrect_Parameter);

    unsigned long bytes_written = 0;
    unsigned char* send_buf = new unsigned char[sprot::implementation::options.max_frame_size];

    FILE* file = fopen(file_name.c_str(), "w");

    sprot::Address recipient;

    try
    {
        while (bytes_written < bytes_to_write)
        {
            sprot::Address fake_ip_port(fake_origin);

            unsigned long how_much = 0;

            if (!sprot::Extended_Transport_Interface::null_data(fake_origin))
            {
                fill_buffer_with_frame_and_random_data(send_buf, static_cast<unsigned short>(sprot::implementation::options.mtu), fake_ip_port.port, fake_ip_port.ip, rng);
                how_much = (sprot::implementation::options.max_frame_size < (bytes_to_write - bytes_written)) ? sprot::implementation::options.max_frame_size : (bytes_to_write - bytes_written);
            }
            else
            {
                randomize_buffer(send_buf, sprot::implementation::options.mtu, rng);
                how_much = (sprot::implementation::options.mtu < (bytes_to_write - bytes_written)) ? sprot::implementation::options.mtu : (bytes_to_write - bytes_written);
            }

            unsigned long current_bytes = 0;

            recipient = fake_origin;
            recipient.port = real_recipient_listen_port;

            if (extended)
                current_bytes = extended->write(send_buf, how_much, recipient, 10000);
            else
                current_bytes = basic->write(send_buf, how_much, 10000);

            if (current_bytes != how_much)
                THROW(fplog::exceptions::Write_Failed);

            fwrite(send_buf, current_bytes, 1, file);

            bytes_written += current_bytes;

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch (fplog::exceptions::Generic_Exception& e)
    {
        std::cout << e.what();

        fflush(file);
        fclose(file);

        delete [] send_buf;
        return bytes_written;
    }

    fflush(file);
    fclose(file);

    delete [] send_buf;
    return bytes_written;
}

unsigned long read_from_transport(unsigned int bytes_to_read, std::string file_name, sprot::Basic_Transport_Interface* basic = nullptr,
                         sprot::Extended_Transport_Interface* extended = nullptr,
                         sprot::Address& origin = sprot::no_address)
{
    if (bytes_to_read == 0)
        return 0;

    if ((basic == nullptr) && (extended == nullptr))
        THROW(fplog::exceptions::Transport_Missing);

    if (extended && sprot::Extended_Transport_Interface::null_data(origin))
        THROW(fplog::exceptions::Incorrect_Parameter);

    unsigned long bytes_read = 0;
    unsigned char* read_buf = new unsigned char[sprot::implementation::options.max_frame_size];

    FILE* file = fopen(file_name.c_str(), "w");

    try
    {
        while (bytes_read < bytes_to_read)
        {
            unsigned long current_bytes = 0;

            if ((bytes_to_read - bytes_read) == 43)
                current_bytes = 0;

            if (extended)
                current_bytes = extended->read(read_buf, sprot::implementation::options.max_frame_size, origin, 10000);
            else
                current_bytes = basic->read(read_buf, sprot::implementation::options.max_frame_size, 10000);

            if (current_bytes == 0)
                THROW(fplog::exceptions::Read_Failed);

            fwrite(read_buf, current_bytes, 1, file);

            bytes_read += current_bytes;
        }
    }
    catch (fplog::exceptions::Generic_Exception& e)
    {
        std::cout << e.what();

        fflush(file);
        fclose(file);

        delete [] read_buf;
        return bytes_read;
    }

    fflush(file);
    fclose(file);

    delete [] read_buf;
    return bytes_read;
}

TEST(Udp_Transport_Test, DISABLED_Smoke_Test)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

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

    const char* message = "hello world?";
    memcpy(send_buf, message, strlen(message));

    sprot::Address recepient, origin;
    recepient.ip = 0x0100007f;
    recepient.port = 26259;

    t1.write(send_buf, strlen(message), recepient);

    size_t read_bytes = t2.read(recv_buf, sizeof(recv_buf), origin);
    EXPECT_EQ(read_bytes, strlen(message));
    EXPECT_EQ(memcmp(message, recv_buf, strlen(message)), 0);
}

TEST(Udp_Transport_Test, DISABLED_Read_Write_1x1)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

    p.first = "chaos";
    p.second = "0";
    params.insert(p);

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    t1.enable(params);

    params["port"] = "26261";
    t2.enable(params);

    unsigned long read_bytes1 = 0;
    unsigned long sent_bytes1 = 0;

    std::thread reader1([&]{

        sprot::Address origin;
        origin.ip = 0x0100007f;
        origin.port = 26261;

        read_bytes1 = read_from_transport(12621400, std::string("reader3.txt"), nullptr, &t1, origin);
    });

    std::thread writer1([&]{

        sprot::Address recepient;
        recepient.ip = 0x0100007f;
        recepient.port = 26260;

        sent_bytes1 = write_to_transport(12621400, std::string("writer3.txt"), &g_rng1, nullptr, &t2, recepient, recepient.port);
    });

    writer1.join();
    reader1.join();

    EXPECT_TRUE(generic_util::compare_files("reader3.txt", "writer3.txt"));
}

TEST(Udp_Transport_Test, DISABLED_Read_Write_Same_Socket)
{
    sprot::Udp_Transport t1;

    sprot::Params params;
    sprot::Param p;

    p.first = "chaos";
    p.second = "0";
    params.insert(p);

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    t1.enable(params);

    unsigned long read_bytes1 = 0;
    unsigned long sent_bytes1 = 0;

    std::thread reader1([&]{

        sprot::Address origin;
        origin.ip = 0x0100007f;
        origin.port = 26260;

        read_bytes1 = read_from_transport(12621400, std::string("reader1.txt"), nullptr, &t1, origin);
    });

    std::thread writer1([&]{

        sprot::Address recepient;
        recepient.ip = 0x0100007f;
        recepient.port = 26260;

        sent_bytes1 = write_to_transport(12621400, std::string("writer1.txt"), &g_rng1, nullptr, &t1, recepient, recepient.port);
    });

    writer1.join();
    reader1.join();

    EXPECT_TRUE(generic_util::compare_files("reader1.txt", "writer1.txt"));
}

TEST(L1_Transport_Test, DISABLED_Smoke_Test)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

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

    const char* message = "hello world?";

    sprot::implementation::Frame frame;
    frame.details.data_len = static_cast<unsigned short>(strlen(message));
    sprintf(frame.details.hostname, "WORKSTATION-666");
    frame.details.type = sprot::implementation::Frame_Type::Data_Frame;
    frame.details.sequence = 999;
    frame.details.origin_ip = 0x0100007f;
    frame.details.origin_listen_port = 26261;

    memcpy(send_buf, frame.bytes, sizeof(frame.bytes));
    memcpy(send_buf + sizeof(frame.bytes), message, strlen(message));

    unsigned short crc = generic_util::gen_simple_crc16(send_buf + 2, static_cast<unsigned short>(sizeof(frame.bytes) + strlen(message)) - 2);
    unsigned short *pcrc = reinterpret_cast<unsigned short*>(&send_buf[0]);
    *pcrc = crc;

    sprot::Address recepient, origin, unknown_origin;
    recepient.ip = 0x0100007f;
    recepient.port = 26260;

    origin.ip = 0x0100007f;
    origin.port = 26261;


    sprot::Packet_Router r1(&t1);
    bool sending = true;

    std::thread sender([&]
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sprot::Packet_Router r2(&t2);

        while (sending)
            r2.write(send_buf, strlen(message) + sizeof (frame.bytes), recepient);
    });

    size_t received_bytes = r1.read(recv_buf, sizeof (recv_buf), unknown_origin);

    frame.details.crc = crc;

    {
        sprot::implementation::Frame received_frame;
        memcpy(received_frame.bytes, recv_buf, sizeof(received_frame.bytes));

        EXPECT_EQ(received_bytes, strlen(message) + sizeof (frame.bytes));
        EXPECT_EQ(memcmp(frame.bytes, received_frame.bytes, sizeof(frame.bytes)), 0);
        EXPECT_EQ(memcmp(message, recv_buf + sizeof(frame.bytes), strlen(message)), 0);
    }

    memset(recv_buf, 0, sizeof(recv_buf));

    received_bytes = r1.read(recv_buf, sizeof (recv_buf), origin);

    sending = false;

    {
        sprot::implementation::Frame received_frame;
        memcpy(received_frame.bytes, recv_buf, sizeof(received_frame.bytes));

        EXPECT_EQ(received_bytes, strlen(message) + sizeof (frame.bytes));
        EXPECT_EQ(memcmp(frame.bytes, received_frame.bytes, sizeof(frame.bytes)), 0);
        EXPECT_EQ(memcmp(message, recv_buf + sizeof(frame.bytes), strlen(message)), 0);

        sprot::Address tuple1(origin), tuple2(unknown_origin);
        EXPECT_EQ(tuple1, tuple2);
    }

    sender.join();
}


TEST(L1_Transport_Test, DISABLED_Multithreaded_Read_Write_3x3)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    t1.enable(params);

    params["port"] = "26261";

    t2.enable(params);

    sprot::Packet_Router r1(&t1);
    sprot::Packet_Router r2(&t2);

    unsigned long read_bytes1 = 0, read_bytes2 = 0, read_bytes3 = 0;
    unsigned long sent_bytes1 = 0, sent_bytes2 = 0, sent_bytes3 = 0;

    std::thread reader1([&]{
        sprot::Address origin_data;
        origin_data.ip = 0x0100007f;
        origin_data.port = 26262;

        read_bytes1 = read_from_transport(2621440, std::string("reader1.txt"), nullptr, &r1, origin_data);
    });

    std::thread reader2([&]{
        sprot::Address origin_data;
        origin_data.ip = 0x0100007f;
        origin_data.port = 26263;

        read_bytes2 = read_from_transport(2621440, std::string("reader2.txt"), nullptr, &r1, origin_data);
    });

    std::thread reader3([&]{
        sprot::Address origin_data;
        origin_data.ip = 0x0100007f;
        origin_data.port = 26264;

        read_bytes3 = read_from_transport(2621440, std::string("reader3.txt"), nullptr, &r1, origin_data);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::thread writer1([&]{
        sprot::Address origin_data;
        origin_data.ip = 0x0100007f;
        origin_data.port = 26262;

        sent_bytes1 = write_to_transport(2621440, std::string("writer1.txt"), &g_rng1, nullptr, &r2, origin_data, 26260);
    });

    std::thread writer2([&]{
        sprot::Address origin_data;
        origin_data.ip = 0x0100007f;
        origin_data.port = 26263;

        sent_bytes2 = write_to_transport(2621440, std::string("writer2.txt"), &g_rng2, nullptr, &r2, origin_data, 26260);
    });

    std::thread writer3([&]{
        sprot::Address origin_data;
        origin_data.ip = 0x0100007f;
        origin_data.port = 26264;

        sent_bytes3 = write_to_transport(2621440, std::string("writer3.txt"), &g_rng3, nullptr, &r2, origin_data, 26260);
    });

    writer1.join();
    writer2.join();
    writer3.join();

    reader1.join();
    reader2.join();
    reader3.join();

    EXPECT_TRUE(generic_util::compare_files("reader1.txt", "writer1.txt"));
    EXPECT_TRUE(generic_util::compare_files("reader2.txt", "writer2.txt"));
    EXPECT_TRUE(generic_util::compare_files("reader3.txt", "writer3.txt"));
}

TEST(Protocol_Test, DISABLED_Smoke_Test)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    t1.enable(params);

    params["port"] = "26261";

    t2.enable(params);

    sprot::Packet_Router r1(&t1);
    sprot::Packet_Router r2(&t2);

    sprot::implementation::Protocol p1(&r1);
    sprot::implementation::Protocol p2(&r2);

    std::thread reader([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26260;

        EXPECT_NO_THROW(p2.accept(params, remote, 5000));

        unsigned char* buf = new unsigned char[sprot::implementation::options.max_frame_size];

        EXPECT_NO_THROW(p2.read(buf, sprot::implementation::options.max_frame_size, 5000));
        EXPECT_EQ(strcmp("hello world", reinterpret_cast<char*>(buf)), 0);

        delete [] buf;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    sprot::Address remote;
    remote.ip = 0x0100007f;
    remote.port = 26261;

    params["port"] = "26260";
    EXPECT_NO_THROW(p1.connect(params, remote, 5000));

    unsigned char buf[50];
    sprintf(reinterpret_cast<char*>(buf), "hello world");

    EXPECT_NO_THROW(p1.write(buf, strlen("hello world") + 1, 5000));

    reader.join();
}

TEST(Protocol_Test, DISABLED_Accept_Unspecified_Connection_Test)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    t1.enable(params);

    params["port"] = "26261";

    t2.enable(params);

    sprot::Packet_Router r1(&t1);
    sprot::Packet_Router r2(&t2);

    sprot::implementation::Protocol p1(&r1);
    sprot::implementation::Protocol p2(&r2);

    std::thread reader([&]{
        sprot::Address remote;
        remote.ip = 0;
        remote.port = 0;

        //Accepting connection from any IP/Port

        EXPECT_NO_THROW(p2.accept(params, remote, 5000));

        EXPECT_EQ(remote.ip, 0x0100007f);
        EXPECT_EQ(remote.port, 26260);

        unsigned char* buf = new unsigned char[sprot::implementation::options.max_frame_size];

        EXPECT_NO_THROW(p2.read(buf, sprot::implementation::options.max_frame_size, 5000));
        EXPECT_EQ(strcmp("hello world", reinterpret_cast<char*>(buf)), 0);

        delete []  buf;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    sprot::Address remote;
    remote.ip = 0x0100007f;
    remote.port = 26261;

    params["port"] = "26260";
    EXPECT_NO_THROW(p1.connect(params, remote, 5000));

    unsigned char buf[50];
    sprintf(reinterpret_cast<char*>(buf), "hello world");

    EXPECT_NO_THROW(p1.write(buf, strlen("hello world") + 1, 5000));

    reader.join();
}

TEST(Protocol_Test, DISABLED_Multithreaded_Read_Write_1x1_No_Simulated_Errors)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

    p.first = "chaos";
    p.second = "0";
    params.insert(p);

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    t1.enable(params);

    params["port"] = "26261";
    params["chaos"] = "0";

    t2.enable(params);

    sprot::Packet_Router r1(&t1);
    sprot::Packet_Router r2(&t2);

    sprot::implementation::Protocol p1(&r1);
    sprot::implementation::Protocol p2(&r2);

    unsigned long read_bytes1 = 0;
    unsigned long sent_bytes1 = 0;

    std::thread reader1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26260;

        EXPECT_NO_THROW(p2.accept(params, remote, 15000));

        read_bytes1 = read_from_transport(1262140, std::string("reader2.txt"), &p2);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::thread writer1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26261;

        params["port"] = "26260";
        EXPECT_NO_THROW(p1.connect(params, remote, 15000));

        sent_bytes1 = write_to_transport(1262140, std::string("writer2.txt"), &g_rng1, &p1);
    });

    writer1.join();

    reader1.join();

    EXPECT_TRUE(generic_util::compare_files("reader2.txt", "writer2.txt"));
}

TEST(Protocol_Test, DISABLED_Multithreaded_Read_Write_1x1_Simulated_Errors)
{
    sprot::Udp_Transport t1, t2;

    sprot::Params params;
    sprot::Param p;

    p.first = "chaos";
    p.second = "124";
    params.insert(p);

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    t1.enable(params);

    params["port"] = "26261";
    params["chaos"] = "50";

    t2.enable(params);

    sprot::Packet_Router r1(&t1);
    sprot::Packet_Router r2(&t2);

    sprot::implementation::Protocol p1(&r1);
    sprot::implementation::Protocol p2(&r2);

    unsigned long read_bytes1 = 0;
    unsigned long sent_bytes1 = 0;

    std::thread reader1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26260;

        EXPECT_NO_THROW(p2.accept(params, remote, 15000));

        read_bytes1 = read_from_transport(4096000, std::string("reader1.txt"), &p2);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::thread writer1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26261;

        params["port"] = "26260";
        EXPECT_NO_THROW(p1.connect(params, remote, 15000));

        sent_bytes1 = write_to_transport(4096000, std::string("writer1.txt"), &g_rng1, &p1);
    });

    writer1.join();

    reader1.join();

    EXPECT_TRUE(generic_util::compare_files("reader1.txt", "writer1.txt"));
}

TEST(Map_Test, DISABLED_Map_As_Key)
{
    sprot::Params params;

    params["max_frame_size"] = "2096";
    params["no_ack_count"] = "4";
    params["storage_max"] = "89";
    params["storage_trim"] = "30";
    params["op_timeout"] = "200";
    params["max_retries"] = "10";

    std::map<sprot::Params, int> test_map;

    test_map[params] = 13;

    sprot::Params params2(params);

    params2["op_timeout"] = "300";

    test_map[params2] = 28;

    params["op_timeout"] = "200";

    EXPECT_EQ(test_map[params], 13);

    params["op_timeout"] = "300";

    EXPECT_EQ(test_map[params], 28);
}

TEST(Options_Test, DISABLED_Load_From_Params)
{
    //storing current options
    sprot::implementation::Options saved(sprot::implementation::options);

    sprot::Params params;

    params["max_frame_size"] = "2096";
    params["no_ack_count"] = "4";
    params["storage_max"] = "89";
    params["storage_trim"] = "30";
    params["op_timeout"] = "200";
    params["max_retries"] = "10";

    sprot::implementation::options.Load(params);

    EXPECT_EQ(sprot::implementation::options.max_frame_size, 2096);
    EXPECT_EQ(sprot::implementation::options.mtu, (sprot::implementation::options.max_frame_size - sizeof(sprot::implementation::Frame::bytes)));
    EXPECT_EQ(sprot::implementation::options.no_ack_count, 4);
    EXPECT_EQ(sprot::implementation::options.storage_max, 89);
    EXPECT_EQ(sprot::implementation::options.storage_trim, 30);
    EXPECT_EQ(sprot::implementation::options.op_timeout, 200);
    EXPECT_EQ(sprot::implementation::options.max_retries, 10);

    params["mtu"] = "666";
    sprot::implementation::options.Load(params);

    EXPECT_EQ(sprot::implementation::options.max_frame_size, 2096);
    //mtu option should have no effect because this value should only be derived from max_frame_size
    EXPECT_EQ(sprot::implementation::options.mtu, (sprot::implementation::options.max_frame_size - sizeof(sprot::implementation::Frame::bytes)));
    EXPECT_EQ(sprot::implementation::options.no_ack_count, 4);
    EXPECT_EQ(sprot::implementation::options.storage_max, 89);
    EXPECT_EQ(sprot::implementation::options.storage_trim, 30);
    EXPECT_EQ(sprot::implementation::options.op_timeout, 200);
    EXPECT_EQ(sprot::implementation::options.max_retries, 10);

    sprot::implementation::options = saved;

    EXPECT_EQ(sprot::implementation::options.max_frame_size, 4096);
    EXPECT_EQ(sprot::implementation::options.mtu, (sprot::implementation::options.max_frame_size - sizeof(sprot::implementation::Frame::bytes)));
    EXPECT_EQ(sprot::implementation::options.no_ack_count, 5);
    EXPECT_EQ(sprot::implementation::options.storage_max, 100);
    EXPECT_EQ(sprot::implementation::options.storage_trim, 50);
    EXPECT_EQ(sprot::implementation::options.op_timeout, 500);
    EXPECT_EQ(sprot::implementation::options.max_retries, 20);
}

TEST(Sessions_Test, DISABLED_Connect_Accept_Read_Write)
{
    sprot::Session_Manager mgr;

    sprot::Params params;
    sprot::Param p;

    p.first = "chaos";
    p.second = "0";
    params.insert(p);

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    unsigned long read_bytes1 = 0;
    unsigned long sent_bytes1 = 0;

    std::thread reader1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26261;

        std::unique_ptr<sprot::Session> s1(mgr.accept(params, remote, 15000));
        read_bytes1 = read_from_transport(1262140, std::string("reader2.txt"), s1.get());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::thread writer1([&]{

        params["port"] = "26261";
        params["chaos"] = "0";

        sprot::Address remote;
        remote.ip = 0x0100007f;
        remote.port = 26260;

        std::unique_ptr<sprot::Session> s2(mgr.connect(params, remote, 15000));

        sent_bytes1 = write_to_transport(1262140, std::string("writer2.txt"), &g_rng1, s2.get());
    });

    writer1.join();

    reader1.join();

    EXPECT_TRUE(generic_util::compare_files("reader2.txt", "writer2.txt"));
}

TEST(Sessions_Test, DISABLED_Large_Transfer)
{
    sprot::Session_Manager mgr;

    sprot::Params params;
    sprot::Param p;

    //configuring local transport for session s1
    //we will open UDP socket on 127.0.0.1 and port 26260
    //it will be used both for accepting connections and reading/writing data

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26260";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    unsigned long read_bytes1 = 0;
    unsigned long sent_bytes1 = 0;

    //will be sending and receiveing 5 megs of randomized data
    const size_t _5mb = 5 * 1024 * 1024;

    //launching accept connection and receiving data in a separate thread
    std::thread reader1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f; //accepting connection only from IP = 127.0.0.1
        remote.port = 26261; //and port = 26261 combination

        bool caught_exception = false;
        std::unique_ptr<unsigned char[]> read_data(new unsigned char[_5mb]);

        std::shared_ptr<sprot::Session> s1(mgr.accept(params, remote, 15000));

        try
        {
            //here we expect the exception because we provided buffer that is too small
            read_bytes1 = s1->read(read_data.get(), _5mb/2, 15000);
        }
        catch(fplog::exceptions::Buffer_Overflow& e)
        {
            caught_exception = true;
            //catch exception and check that it provided us with the buffer size that will fit all data
            EXPECT_EQ(_5mb, e.get_required_size());
        }

        EXPECT_EQ(caught_exception, true);

        //reading these 5 megs again with properly sized buffer
        read_bytes1 = s1->read(read_data.get(), _5mb, 15000);

        //storing received data to file in order to compare and check for inconsistencies
        //with the data that has been sent, obviously these two should be identical
        FILE* file = fopen("reader3.txt", "w");
        fwrite(read_data.get(), read_bytes1, 1, file);
        fclose(file);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    //launching connect and sending data in a separate thread
    std::thread writer1([&]{

        params["port"] = "26261"; //this is the local port of the sending socket for session s2
        //when a session is created, it is associated with a local socket
        //that is being created at the same time with the session or reused if
        //there already is a socket of the same IP/Port combination inside
        //the Session_Manager internals

        sprot::Address remote;
        remote.ip = 0x0100007f;//connecting to IP = 127.0.0.1
        remote.port = 26260;//on port = 26260

        //connecting to the session s1 that is already listening and waiting to accept connection
        std::unique_ptr<sprot::Session> s2(mgr.connect(params, remote, 15000));

        std::unique_ptr<unsigned char[]> random_5mb(new unsigned char[_5mb]);
        randomize_buffer(random_5mb.get(), _5mb, &g_rng1);

        //sending random 5 megs to session s1
        sent_bytes1 = s2->write(random_5mb.get(), _5mb, 15000);

        //storing sent data to file to compare later with the data that was received
        //in case there were no errors both should match
        FILE* file = fopen("writer3.txt", "w");
        fwrite(random_5mb.get(), _5mb, 1, file);
        fclose(file);
    });

    writer1.join();
    reader1.join();

    //checking if sent data matches received data, there should be no differences
    EXPECT_TRUE(generic_util::compare_files("reader3.txt", "writer3.txt"));
}

TEST(Piped_Sequence_Test, DISABLED_Get_Sequence_Number)
{
    using namespace sequence_number;

    unsigned long long s1 = read_sequence_number();
    unsigned long long s2 = read_sequence_number();
    unsigned long long s3 = read_sequence_number();

    EXPECT_GT(s1, 0);
    EXPECT_GT(s2, s1);
    EXPECT_GT(s3, s2);
}

class Bar
{
    public:
        virtual bool FooBar() = 0;
        virtual ~Bar(){}
};

class Foo: public Bar
{
    public:

        bool FooBar()
        {
            fplog::write(FPL_CINFO("blah-blah %d!", 38).add("real", -9.54));
            return true;
        }
};

void prepare_api_test()
{
    fplog::g_test_results_vector.clear();

    fplog::openlog(fplog::Facility::security, new fplog::Priority_Filter("prio_filter"));

    fplog::Priority_Filter* f = dynamic_cast<fplog::Priority_Filter*>(fplog::find_filter("prio_filter"));
    EXPECT_NE(f, nullptr);

    f->add_all_above("debug", true);
}

TEST(Fplog_Api_Test, DISABLED_Method_And_Class_Logging)
{
    prepare_api_test();

    Foo k;
    k.FooBar();

    std::string good("{\"priority\":\"info\",\"facility\":\"security\",\"text\":\"blah-blah 38!\",\"module\":\"main.cpp\",\"line\":1069,\"method\":\"FooBar\",\"class\":\"Foo\",\"real\":-9.54,\"appname\":\"fplog_test\"}");

    EXPECT_EQ(fplog::g_test_results_vector[0], good);

    fplog::closelog();
}

TEST(Fplog_Api_Test, DISABLED_Trim_And_Blob)
{
    prepare_api_test();

    int var = -533;
    int var2 = 54674;
    fplog::write(fplog::Message(fplog::Prio::alert, fplog::Facility::system, "go fetch some numbers").
                 add("blob", 66).add("int", 23).add_binary("int_bin", &var, sizeof(int)).
                 add_binary("int_bin", &var2, sizeof(int)).add("    Double ", -1.23).add("encrypted", "sfewre"));

   std::string good("{\"priority\":\"alert\",\"facility\":\"system\",\"text\":\"go fetch some numbers\",\"warning\":\"Some parameters are missing from this log message because they were malformed.\",\"int\":23,\"int_bin\":{\"blob\":\"ktUAAA==\"},\"Double\":-1.23,\"appname\":\"fplog_test\"}");

   EXPECT_EQ(fplog::g_test_results_vector[0], good);

   fplog::closelog();
}

TEST(Fplog_Api_Test, DISABLED_Send_File)
{
    prepare_api_test();

    const char* str = "asafdkfj *** Hello, world! -=-=-=-=-=-+++   ";

    fplog::write(fplog::File(fplog::Prio::alert, "dump.bin", str, strlen(str)).as_message());

    std::string good("{\"priority\":\"alert\",\"facility\":\"user\",\"file\":\"dump.bin\",\"text\":\"YXNhZmRrZmogKioqIEhlbGxvLCB3b3JsZCEgLT0tPS09LT0tPS0rKysgICA=\",\"appname\":\"fplog_test\"}");

    EXPECT_EQ(fplog::g_test_results_vector[0], good);

    fplog::closelog();
}

TEST(Fplog_Api_Test, DISABLED_Filters)
{
    prepare_api_test();

    fplog::Priority_Filter* filter = dynamic_cast<fplog::Priority_Filter*>(fplog::find_filter("prio_filter"));
    if (!filter)
    {
        EXPECT_NE(filter, nullptr);
        return;
    }

    filter->remove();

    {
        fplog::Message msg(fplog::Prio::alert, fplog::Facility::system, "this message should not appear");
        fplog::write(msg);
    }

    filter->add(fplog::Prio::emergency);
    filter->add(fplog::Prio::debug);

    {
        fplog::Message msg(fplog::Prio::alert, fplog::Facility::system, "this message still should not appear");
        fplog::write(msg);
    }
    {
        fplog::Message msg(fplog::Prio::emergency, fplog::Facility::system, "this emergency message is visible");
        fplog::write(msg);
    }
    {
        fplog::Message msg(fplog::Prio::debug, fplog::Facility::system, "along with this debug message");
        fplog::write(msg);
    }

    filter->remove(fplog::Prio::emergency);

    {
        fplog::Message msg(fplog::Prio::emergency, fplog::Facility::system, "this is invisible emergency");
        fplog::write(msg);
    }

    remove_filter(filter);

    {
        fplog::Message msg(fplog::Prio::debug, fplog::Facility::system, "this debug message should be invisible");
        fplog::write(msg);
    }

    EXPECT_EQ(fplog::g_test_results_vector.size(), 2);
    EXPECT_EQ("{\"priority\":\"emergency\",\"facility\":\"system\",\"text\":\"this emergency message is visible\",\"appname\":\"fplog_test\"}", fplog::g_test_results_vector[0]);
    EXPECT_EQ("{\"priority\":\"debug\",\"facility\":\"system\",\"text\":\"along with this debug message\",\"appname\":\"fplog_test\"}", fplog::g_test_results_vector[1]);

    fplog::closelog();
}

static std::string strip_timestamp_and_sequence(std::string input)
{
    generic_util::remove_json_field(fplog::Message::Mandatory_Fields::timestamp, input);
    generic_util::remove_json_field(fplog::Message::Optional_Fields::sequence, input);

    return input;
}

TEST(Fplog_Api_Test, Batching)
{
    prepare_api_test();

    rapidjson::Document batch, named_batch, m1, m2;

    batch.SetArray();

    auto batch_array = batch.GetArray();

    m1.CopyFrom(fplog::Message(fplog::Prio::debug, fplog::Facility::user, "batching test msg #1").as_json(), batch.GetAllocator());
    m2.CopyFrom(fplog::Message(fplog::Prio::debug, fplog::Facility::user, "batching test msg #2").as_json(), batch.GetAllocator());

    batch_array.PushBack(m1, batch.GetAllocator());
    batch_array.PushBack(m2, batch.GetAllocator());

    fplog::Message batch_msg(fplog::Prio::debug, fplog::Facility::user);

    named_batch.SetObject();
    named_batch.AddMember("batch", batch, named_batch.GetAllocator());

    batch_msg.add_batch(named_batch);

    rapidjson::Document json_msg(batch_msg.as_json());

    //skipping 3 members: timestamp, priority, facility and jumping straight to 4th which is batch array
    auto it((++++++(json_msg.MemberBegin()))->value.GetArray().begin());

    fplog::g_test_results_vector.push_back(strip_timestamp_and_sequence(batch_msg.as_string()));
    int counter = 0;

    EXPECT_NE(it, (++++++(json_msg.MemberBegin()))->value.GetArray().end());

    while (it != (++++++(json_msg.MemberBegin()))->value.GetArray().end())
    {
        {
            rapidjson::Document json;
            json.SetObject();

            rapidjson::GenericValue<rapidjson::UTF8<>, rapidjson::MemoryPoolAllocator<>> name(std::to_string(counter).c_str(), json.GetAllocator());

            json.AddMember(name, *it, json.GetAllocator());
            rapidjson::StringBuffer s;
            rapidjson::Writer<rapidjson::StringBuffer> w(s);
            json.Accept(w);
            fplog::g_test_results_vector.push_back(s.GetString());
        }
        ++it;
        counter++;
    }

    fplog::g_test_results_vector.push_back("msg has batch? = " + std::to_string(batch_msg.has_batch()));

    fplog::Message batch_clone(batch_msg.as_string());

    fplog::g_test_results_vector.push_back("cloned msg: " + strip_timestamp_and_sequence(batch_clone.as_string()));
    fplog::g_test_results_vector.push_back("clone msg has batch? = " + std::to_string(batch_clone.has_batch()));

    EXPECT_EQ(fplog::g_test_results_vector[3], "msg has batch? = 1");
    EXPECT_EQ(fplog::g_test_results_vector[4], "cloned msg: {\"priority\":\"debug\",\"facility\":\"user\",\""
                                               "batch\":[{\"priority\":\"debug\",\"facility\":\"user\",\"text\":\""
                                               "batching test msg #1\"},{\"priority\":\"debug\",\"facility\":\"user\","
                                               "\"text\":\"batching test msg #2\"}]}");
    EXPECT_EQ(fplog::g_test_results_vector[5], "clone msg has batch? = 1");

    fplog::closelog();
}

TEST(Queue_Controller_Test, DISABLED_Remove_Newest)
{
    Queue_Controller qc(200, 3000);
    qc.change_algo(std::make_shared<Queue_Controller::Remove_Newest>(qc), Queue_Controller::Algo::Fallback_Options::Remove_Oldest);

    std::string msg("Ten bytes.");

    for (int i = 0; i < 30; ++i)
        qc.push(new std::string(msg));

    std::vector<std::string*> v;

    while (!qc.empty())
    {
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 30)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 30);
    }

    qc.push(new std::string("The oldest one"));
    qc.push(new std::string("The oldest two"));

    for (int i = 0; i < 28; ++i)
        qc.push(new std::string(msg));

    std::this_thread::sleep_for(chrono::seconds(4));

    qc.push(new std::string(msg));
    v.clear();

    while (!qc.empty())
    {
        v.push_back(qc.front());
        //cout << *qc.front() << std::endl;
        qc.pop();
    }

    if (v.size() != 20)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 20);
    }

    bool correct_found = false;
    for (std::vector<std::string*>::iterator it(v.begin()); it != v.end(); ++it)
    {
        if ((**it).find("The oldest one") != string::npos)
            correct_found = true;
    }

    if (!correct_found)
    {
        cout << "Remove_Newest: unable to locate expected string" << endl;
        EXPECT_TRUE(correct_found);
    }
}

TEST(Queue_Controller_Test, DISABLED_Remove_Oldest)
{
    Queue_Controller qc(200, 3000);

    qc.change_algo(std::make_shared<Queue_Controller::Remove_Oldest>(qc), Queue_Controller::Algo::Fallback_Options::Remove_Newest);

    qc.push(new std::string("Old msg 1."));
    qc.push(new std::string("Old msg 2."));
    qc.push(new std::string("Old msg 3."));
    qc.push(new std::string("Old msg 4."));
    qc.push(new std::string("Old msg 5."));
    qc.push(new std::string("Old msg 6."));
    qc.push(new std::string("Old msg 7."));
    qc.push(new std::string("Old msg 8."));
    qc.push(new std::string("Old msg 9."));
    qc.push(new std::string("Old msg 10"));

    std::string msg("Ten bytes.");

    for (int i = 0; i < 14; ++i)
        qc.push(new std::string(msg));

    std::this_thread::sleep_for(chrono::seconds(4));

    qc.push(new std::string(msg));

    std::vector<std::string*> v;
    while (!qc.empty())
    {
        v.push_back(qc.front());
        //cout << *qc.front() << std::endl;
        qc.pop();
    }

    bool correct_found = false;
    for (std::vector<std::string*>::iterator it(v.begin()); it != v.end(); ++it)
    {
        if ((**it).find("Old msg 5.") != string::npos)
        {
            cout << "Remove_Oldest: incorrect string detected: " << **it << std::endl;
            EXPECT_EQ((**it).find("Old msg 5."), string::npos);
        }

        if ((**it).find("Old msg 6.") != string::npos)
            correct_found = true;
    }

    if (!correct_found)
    {
        cout << "Remove_Oldest: unable to locate expected string" << endl;
        EXPECT_TRUE(correct_found);
    }

    if (v.size() != 20)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 20);
    }
}

TEST(Queue_Controller_Test, DISABLED_Remove_Newest_Below_Prio)
{
    std::minstd_rand rng;
    rng.seed(21);

    Queue_Controller qc(3600, 3000);
    qc.change_algo(std::make_shared<Queue_Controller::Remove_Newest_Below_Priority>(qc, fplog::Prio::warning), Queue_Controller::Algo::Fallback_Options::Remove_Newest);

    std::string msg("Ten bytes.");

    for (int i = 0; i < 30; ++i)
    {
        unsigned int r = rng();
        r = r % 4;

        if (r == 0)
            qc.push(new std::string(FPL_TRACE(msg.c_str()).as_string()));

        if (r == 1)
            qc.push(new std::string(FPL_INFO(msg.c_str()).as_string()));

        if (r == 2)
            qc.push(new std::string(FPL_WARN(msg.c_str()).as_string()));

        if (r == 3)
            qc.push(new std::string(FPL_ERROR(msg.c_str()).as_string()));
    }

    std::vector<std::string*> v;

    while (!qc.empty())
    {
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 30)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 30);
    }

    for (int i = 0; i < 30; ++i)
    {
        unsigned int r = rng();
        r = r % 4;

        std::string* str = nullptr;

        if (r == 0)
            str = new std::string(FPL_TRACE(msg.c_str()).add("num", i).as_string());

        if (r == 1)
            str = new std::string(FPL_INFO(msg.c_str()).add("num", i).as_string());

        if (r == 2)
            str = new std::string(FPL_WARN(msg.c_str()).add("num", i).as_string());

        if (r == 3)
            str = new std::string(FPL_ERROR(msg.c_str()).add("num", i).as_string());

        qc.push(str);

        //std::cout << *str << std::endl;
    }

    std::this_thread::sleep_for(chrono::seconds(4));

    //std::cout << "************************" << std::endl;

    qc.push(new std::string(msg));
    v.clear();

    while (!qc.empty())
    {
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 23)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 23);
    }

    bool correct_found = false;
    for (std::vector<std::string*>::iterator it(v.begin()); it != v.end(); ++it)
    {
        if ((**it).find("\"num\":26") != string::npos)
        {
            cout << "Remove_Newest_Below_Priority: incorrect string detected: " << **it << std::endl;
            EXPECT_EQ((**it).find("\"num\":26"), string::npos);
        }

        if ((**it).find("\"num\":2") != string::npos)
            correct_found = true;
    }

    if (!correct_found)
    {
        cout << "Remove_Newest_Below_Priority: unable to locate expected string" << endl;
        EXPECT_TRUE(correct_found);
    }
}

TEST(Queue_Controller_Test, DISABLED_Remove_Oldest_Below_Prio)
{
    std::minstd_rand rng;
    rng.seed(13);

    Queue_Controller qc(3600, 3000);
    qc.change_algo(std::make_shared<Queue_Controller::Remove_Oldest_Below_Priority>(qc, fplog::Prio::warning), Queue_Controller::Algo::Fallback_Options::Remove_Oldest);

    std::string msg("Ten bytes.");

    for (int i = 0; i < 30; ++i)
    {
        unsigned int r = rng();
        r = r % 4;

        if (r == 0)
            qc.push(new std::string(FPL_TRACE(msg.c_str()).as_string()));

        if (r == 1)
            qc.push(new std::string(FPL_INFO(msg.c_str()).as_string()));

        if (r == 2)
            qc.push(new std::string(FPL_WARN(msg.c_str()).as_string()));

        if (r == 3)
            qc.push(new std::string(FPL_ERROR(msg.c_str()).as_string()));
    }

    std::vector<std::string*> v;

    while (!qc.empty())
    {
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 30)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 30);
    }

    for (int i = 0; i < 30; ++i)
    {
        unsigned int r = rng();
        r = r % 4;

        std::string* str = nullptr;

        if (r == 0)
            str = new std::string(FPL_TRACE(msg.c_str()).add("num", i).as_string());

        if (r == 1)
            str = new std::string(FPL_INFO(msg.c_str()).add("num", i).as_string());

        if (r == 2)
            str = new std::string(FPL_WARN(msg.c_str()).add("num", i).as_string());

        if (r == 3)
            str = new std::string(FPL_ERROR(msg.c_str()).add("num", i).as_string());

        qc.push(str);

        //std::cout << *str << std::endl;
    }

    std::this_thread::sleep_for(chrono::seconds(4));

    //std::cout << "************************" << std::endl;

    qc.push(new std::string(msg));
    v.clear();

    while (!qc.empty())
    {
        //std::cout << *qc.front() << std::endl;
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 23)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 23);
    }

    bool correct_found = false;
    for (std::vector<std::string*>::iterator it(v.begin()); it != v.end(); ++it)
    {
        if ((**it).find("\"num\":4") != string::npos)
        {
            cout << "Remove_Oldest_Below_Priority: incorrect string detected: " << **it << std::endl;
            EXPECT_EQ((**it).find("\"num\":4"), string::npos);
        }

        if ((**it).find("\"num\":29") != string::npos)
            correct_found = true;
    }

    if (!correct_found)
    {
        cout << "Remove_Oldest_Below_Priority: unable to locate expected string" << endl;
        EXPECT_TRUE(correct_found);
    }
}

TEST(Queue_Controller_Test, DISABLED_Apply_Config)
{
    std::minstd_rand rng;
    rng.seed(21);

    Queue_Controller qc(1, 1);
    qc.change_algo(std::make_shared<Queue_Controller::Remove_Newest>(qc), Queue_Controller::Algo::Fallback_Options::Remove_Newest);

    sprot::Params params;

    params["max_queue_size"] = "3600";
    params["emergency_timeout"] = "3000";
    params["emergency_algo"] = "remove_newest_below_prio";
    params["emergency_fallback_algo"] = "remove_newest";
    params["emergency_prio"] = std::string(fplog::Prio::warning);

    qc.apply_config(params);

    std::string msg("Ten bytes.");

    for (int i = 0; i < 30; ++i)
    {
        unsigned int r = rng();
        r = r % 4;

        if (r == 0)
            qc.push(new std::string(FPL_TRACE(msg.c_str()).as_string()));

        if (r == 1)
            qc.push(new std::string(FPL_INFO(msg.c_str()).as_string()));

        if (r == 2)
            qc.push(new std::string(FPL_WARN(msg.c_str()).as_string()));

        if (r == 3)
            qc.push(new std::string(FPL_ERROR(msg.c_str()).as_string()));
    }

    std::vector<std::string*> v;

    while (!qc.empty())
    {
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 30)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 30);
    }

    for (int i = 0; i < 30; ++i)
    {
        unsigned int r = rng();
        r = r % 4;

        std::string* str = nullptr;

        if (r == 0)
            str = new std::string(FPL_TRACE(msg.c_str()).add("num", i).as_string());

        if (r == 1)
            str = new std::string(FPL_INFO(msg.c_str()).add("num", i).as_string());

        if (r == 2)
            str = new std::string(FPL_WARN(msg.c_str()).add("num", i).as_string());

        if (r == 3)
            str = new std::string(FPL_ERROR(msg.c_str()).add("num", i).as_string());

        qc.push(str);

        //std::cout << *str << std::endl;
    }

    std::this_thread::sleep_for(chrono::seconds(4));

    //std::cout << "************************" << std::endl;

    qc.push(new std::string(msg));
    v.clear();

    while (!qc.empty())
    {
        //std::cout << *qc.front() << std::endl;
        v.push_back(qc.front());
        qc.pop();
    }

    if (v.size() != 23)
    {
        cout << "Incorrect size of queue detected! (" << v.size() << ")." << std::endl;
        EXPECT_EQ(v.size(), 23);
    }

    bool correct_found = false;
    for (std::vector<std::string*>::iterator it(v.begin()); it != v.end(); ++it)
    {
        if ((**it).find("\"num\":26") != string::npos)
        {
            cout << "Remove_Newest_Below_Priority: incorrect string detected: " << **it << std::endl;
            EXPECT_EQ((**it).find("\"num\":26"), string::npos);
        }

        if ((**it).find("\"num\":2") != string::npos)
            correct_found = true;
    }

    if (!correct_found)
    {
        cout << "Remove_Newest_Below_Priority: unable to locate expected string" << endl;
        EXPECT_TRUE(correct_found);
    }
}


int main(int argc, char **argv)
{
    system("rm /tmp/fplog2_sequence_stop");
    //system("./shared_sequence.sh&");

    debug_logging::g_logger.open("fplog2-test-log.txt");

    /*sprot::Session_Manager mgr;

    sprot::Params params;
    sprot::Param p;

    //configuring local transport for session s1
    //we will open UDP socket on 127.0.0.1 and port 26260
    //it will be used both for accepting connections and reading/writing data

    p.first = "ip";
    p.second = "127.0.0.1";
    params.insert(p);

    p.first = "port";
    p.second = "26360";
    params.insert(p);

    params["hostname"] = "WORKSTATION-666";

    bool stop = false;

    //launching accept connection and receiving data in a separate thread
    std::thread reader1([&]{
        sprot::Address remote;
        remote.ip = 0x0100007f; //accepting connection only from IP = 127.0.0.1
        remote.port = 26361; //and port = 26361 combination

        std::shared_ptr<sprot::Session> s1(mgr.accept(params, remote, 1500));
        std::shared_ptr<char> buf(new char [sprot::implementation::options.mtu]);

        while (!stop)
        {
            memset(buf.get(), 0, sprot::implementation::options.mtu);

            try
            {
                s1->read(buf.get(), sprot::implementation::options.mtu, 400);
                std::cout << buf.get() << "\n";
            }
            catch (...)
            {
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    params["port"] = "26361"; //this is the local port of the sending socket for session s2
    //when a session is created, it is associated with a local socket
    //that is being created at the same time with the session or reused if
    //there already is a socket of the same IP/Port combination inside
    //the Session_Manager internals

    sprot::Address remote;
    remote.ip = 0x0100007f;//connecting to IP = 127.0.0.1
    remote.port = 26360;//on port = 26360

    //connecting to the session s1 that is already listening and waiting to accept connection
    std::unique_ptr<sprot::Session> s2(mgr.connect(params, remote, 1500));*/

    try
    {
        fplog::initlog("fplog_test", nullptr, false);
    }
    catch (fplog::exceptions::Transport_Missing&)
    {
    }

    fplog::g_fplog_impl->set_test_mode(true);

    ::testing::InitGoogleTest(&argc, argv);

    int res = RUN_ALL_TESTS();

    //print_test_vector();

    //stop = true;
    //reader1.join();

    system("touch /tmp/fplog2_sequence_stop");
    sequence_number::read_sequence_number(1000);
    system("rm /tmp/fplog2_sequence_stop");

    return res;
}
