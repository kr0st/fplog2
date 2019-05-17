
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

    size_t received_bytes = r1.read(recv_buf, sizeof (recv_buf), origin);

    frame.details.crc = crc;

    {
        sprot::implementation::Frame received_frame;
        memcpy(received_frame.bytes, recv_buf, sizeof(received_frame.bytes));

        EXPECT_EQ(received_bytes, strlen(message) + sizeof (frame.bytes));
        EXPECT_EQ(memcmp(frame.bytes, received_frame.bytes, sizeof(frame.bytes)), 0);
        EXPECT_EQ(memcmp(message, recv_buf + sizeof(frame.bytes), strlen(message)), 0);
    }

    memset(recv_buf, 0, sizeof(recv_buf));

    received_bytes = r1.read(recv_buf, sizeof (recv_buf), unknown_origin);

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

TEST(Map_Test, Map_As_Key)
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

TEST(Options_Test, Load_From_Params)
{
    sprot::implementation::Options saved(sprot::implementation::options);

    sprot::Params params;
    sprot::Param p;

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

TEST(Sessions_Test, Connect_Accept_Read_Write)
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

int main(int argc, char **argv)
{
    debug_logging::g_logger.open("fplog2-test-log.txt");

    ::testing::InitGoogleTest(&argc, argv);

    int res = RUN_ALL_TESTS();

    return res;
}
