#include <sprot.h>
#include <protocol.h>

namespace sprot {

class Session::Session_Implementation
{
    private:


        implementation::Protocol* proto_ = nullptr;
        unsigned char multipart_magic_sequence_[13];
        unsigned char remembered_magic_[sizeof(multipart_magic_sequence_) + sizeof(size_t)];


    public:

        bool connect(const Params& local_config, const Address& remote, size_t timeout = infinite_wait);
        bool accept(const Params& local_config, Address& remote, size_t timeout = infinite_wait);

        size_t read(void* buf, size_t buf_size, size_t timeout = infinite_wait);
        size_t write(const void* buf, size_t buf_size, size_t timeout = infinite_wait);

        Session_Implementation(Extended_Transport_Interface* l1_transport);
        ~Session_Implementation();

        Session_Configuration config_;
};

bool Session::Session_Implementation::connect(const Params& local_config, const Address& remote, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    memset(remembered_magic_, 0, sizeof(remembered_magic_));

    return proto_->connect(local_config, remote, timeout);
}

bool Session::Session_Implementation::accept(const Params& local_config, Address& remote, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    memset(remembered_magic_, 0, sizeof(remembered_magic_));

    return proto_->accept(local_config, remote, timeout);
}

size_t Session::Session_Implementation::read(void* buf, size_t buf_size, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    size_t bytes_read = 0;

    unsigned char empty_magic[sizeof(remembered_magic_)] = {0};
    if (memcmp(remembered_magic_, empty_magic, sizeof(remembered_magic_)) == 0)
        bytes_read = proto_->read(buf,  buf_size, timeout);
    else
    {
        bytes_read = sizeof(remembered_magic_);
        memcpy(buf, remembered_magic_, sizeof(remembered_magic_));
    }

    if ((bytes_read == (sizeof(multipart_magic_sequence_) + sizeof(size_t))) &&
            (memcmp(buf, multipart_magic_sequence_, sizeof(multipart_magic_sequence_)) == 0))
    {
        memcpy(remembered_magic_, buf, sizeof(remembered_magic_));
        size_t expected_len = 0;
        memcpy(&expected_len, static_cast<unsigned char*>(buf) + sizeof(multipart_magic_sequence_), sizeof(size_t));
        if (buf_size < expected_len)
            THROW2(fplog::exceptions::Buffer_Overflow, "Buffer too small! Use get_required_size() from this exception to find fitting size.", expected_len);

        memset(remembered_magic_, 0, sizeof(remembered_magic_));

        int bytes_left = static_cast<int>(expected_len);
        std::unique_ptr<unsigned char[]> temp_buf(new unsigned char[implementation::options.mtu]);

        unsigned char* buf_ptr = static_cast<unsigned char*>(buf);
        while (bytes_left > 0)
        {
            bytes_read = proto_->read(temp_buf.get(),  implementation::options.mtu, timeout);
            memcpy(buf_ptr, temp_buf.get(), bytes_read);
            buf_ptr += bytes_read;
            bytes_left -= bytes_read;
        }

        if (bytes_left == 0)
            return expected_len;
        else
            THROW1(fplog::exceptions::Read_Failed, "Corrupted multi-part message!");
    }
    else
        return bytes_read;
}

size_t Session::Session_Implementation::write(const void* buf, size_t buf_size, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    if (buf_size > implementation::options.mtu)
    {
        size_t timeout_div = buf_size / sprot::implementation::options.mtu + 2;

        unsigned char magic[sizeof(multipart_magic_sequence_) + sizeof(size_t)];
        memcpy(magic, multipart_magic_sequence_, sizeof(multipart_magic_sequence_));
        memcpy(magic + sizeof(multipart_magic_sequence_), &buf_size, sizeof(size_t));

        if ((sizeof(multipart_magic_sequence_) + sizeof(size_t)) !=
                proto_->write(magic, sizeof(multipart_magic_sequence_) + sizeof(size_t), timeout / timeout_div))
            THROW(fplog::exceptions::Write_Failed);

        unsigned long bytes_written = 0;
        unsigned long bytes_to_write = buf_size;

        while (bytes_written < bytes_to_write)
        {
            unsigned long how_much =
            (sprot::implementation::options.mtu < (bytes_to_write - bytes_written)) ?
                        sprot::implementation::options.mtu : (bytes_to_write - bytes_written);

            unsigned long current_bytes = proto_->write(static_cast<const char*>(buf) + bytes_written, how_much, timeout / timeout_div);

            if (current_bytes != how_much)
                THROW(fplog::exceptions::Write_Failed);

            bytes_written += current_bytes;
        }

        return bytes_written;
    }

    return proto_->write(buf, buf_size, timeout);
}

Session::Session_Implementation::Session_Implementation(Extended_Transport_Interface* l1_transport)
{
    proto_ = new implementation::Protocol(l1_transport);

    multipart_magic_sequence_[0] = 0x12;
    multipart_magic_sequence_[1] = 0xF3;

    multipart_magic_sequence_[2] = 'm';
    multipart_magic_sequence_[3] = 'u';
    multipart_magic_sequence_[4] = 'l';
    multipart_magic_sequence_[5] = 't';
    multipart_magic_sequence_[6] = 'i';
    multipart_magic_sequence_[7] = 'p';
    multipart_magic_sequence_[8] = 'a';
    multipart_magic_sequence_[9] = 'r';
    multipart_magic_sequence_[10] = 't';

    multipart_magic_sequence_[11] = 0x3F;
    multipart_magic_sequence_[12] = 0x21;
}

Session::Session_Implementation::~Session_Implementation()
{
    delete proto_;
}

size_t Session::read(void* buf, size_t buf_size, size_t timeout)
{
    return impl_->read(buf, buf_size, timeout);
}

size_t Session::write(const void* buf, size_t buf_size, size_t timeout)
{
    return impl_->write(buf, buf_size, timeout);
}

void Session::disconnect()
{
}

Session::Session(Extended_Transport_Interface* l1_transport)
{
    impl_ = new Session_Implementation(l1_transport);
}

Session::~Session()
{
    delete impl_;
}

bool Session::connect(const Params& local_config, const Address& remote, size_t timeout)
{
    bool res(impl_->connect(local_config, remote, timeout));

    if (res)
    {
        impl_->config_.local_config = local_config;
        impl_->config_.remote = remote;
    }

    return res;
}

bool Session::accept(const Params& local_config, Address& remote, size_t timeout)
{
    bool res(impl_->accept(local_config, remote, timeout));

    if (res)
    {
        impl_->config_.local_config = local_config;
        impl_->config_.remote = remote;
    }

    return res;
}

Session_Configuration Session::get_config()
{
    return impl_->config_;
}

};
