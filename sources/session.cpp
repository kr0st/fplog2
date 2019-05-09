#include <sprot.h>
#include <protocol.h>

namespace sprot {

class Session::Session_Implementation
{
    private:

        implementation::Protocol* proto_;


    public:

        Session_Implementation();
        ~Session_Implementation();
};

Session::Session_Implementation::Session_Implementation()
{
    proto_ = nullptr;
}

Session::Session_Implementation::~Session_Implementation()
{
    delete proto_;
}

void Session::disconnect()
{
}

Session::Session()
{
    impl_ = new Session_Implementation();
}

Session::~Session()
{
    delete impl_;
}

};
