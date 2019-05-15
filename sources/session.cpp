#include <sprot.h>
#include <protocol.h>

namespace sprot {

class Session::Session_Implementation
{
    private:

        implementation::Protocol* proto_ = nullptr;


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

    return proto_->connect(local_config, remote, timeout);
}

bool Session::Session_Implementation::accept(const Params& local_config, Address& remote, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    return proto_->accept(local_config, remote, timeout);
}

size_t Session::Session_Implementation::read(void* buf, size_t buf_size, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    return proto_->read(buf,  buf_size, timeout);
}

size_t Session::Session_Implementation::write(const void* buf, size_t buf_size, size_t timeout)
{
    if (proto_ == nullptr)
        return 0;

    return proto_->write(buf, buf_size, timeout);
}

Session::Session_Implementation::Session_Implementation(Extended_Transport_Interface* l1_transport)
{
    proto_ = new implementation::Protocol(l1_transport);
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
