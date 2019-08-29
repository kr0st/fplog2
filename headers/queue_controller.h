#include <string>
#include <queue>
#include <memory>
#include <chrono>
#include <sprot.h>

#ifdef FPLOG_EXPORT

#ifdef _LINUX
#define FPLOG_API __attribute__ ((visibility("default")))
#else
#define FPLOG_API __declspec(dllexport)
#endif

#else

#ifdef __linux__
#define FPLOG_API 
#else
#define FPLOG_API __declspec(dllimport)
#endif

#ifdef __APPLE__
#define FPLOG_API
#endif

#endif

using namespace std::chrono;
using namespace std;

class FPLOG_API Queue_Controller
{
    public: 

        class Algo
        {
            public:
            
                struct Result
                {
                    size_t current_size = 0;
                    size_t removed_count = 0;
                };
                
                struct Fallback_Options
                {
                    enum Type
                    {
                        Remove_Newest = 765,
                        Remove_Oldest
                    };
                };
                
                Algo(queue<string*>& mq, size_t max_size, size_t current_size = 0): mq_(mq), max_size_(max_size), current_size_(static_cast<int>(current_size)){}
                virtual Result process_queue(size_t current_size) = 0;


            private:
                
                Algo();
            

            protected:    
            
                queue<string*>& mq_;
                size_t max_size_;
                int current_size_;
        };

        class FPLOG_API Remove_Oldest: public Algo
        {
            public:
                    
                    Remove_Oldest(Queue_Controller& qc): Algo(qc.mq_, qc.max_size_, qc.mq_size_) {}
                    Result process_queue(size_t current_size);
        };        

        class FPLOG_API Remove_Newest;
        class FPLOG_API Remove_Newest_Below_Priority;
        class FPLOG_API Remove_Oldest_Below_Priority;

        Queue_Controller(size_t size_limit = 20000000, size_t timeout = 30000);

        bool empty();
        string *front();
        void pop();
        void push(string *str);
        
        void change_algo(std::shared_ptr<Algo> algo, Algo::Fallback_Options::Type fallback_algo);
        void change_params(size_t size_limit, size_t timeout);
        
        //configuration params as follows:
        //max_queue_size = [any positive integer]
        //emergency_timeout = [any positive integer]
        //emergency_algo = one of { remove_oldest, remove_newest, remove_oldest_below_prio, remove_newest_below_prio }
        //emergency_fallback_algo = one of { remove_oldest, remove_newest }
        //emergency_prio = use one of the fplog::Prio constants //only needed if algo is based on prio
        void apply_config(const sprot::Params& config);


    private:

        int mq_size_ = 0;
        size_t max_size_ = 0;

        queue<string*> mq_;

        std::shared_ptr<Algo> algo_;
        std::shared_ptr<Algo> algo_fallback_;

        size_t emergency_time_trigger_ = 0;
        time_point<system_clock, system_clock::duration> timer_start_;

        bool state_of_emergency();
        void handle_emergency();
        
        std::shared_ptr<Algo> make_algo(const std::string& name, const std::string& param);
};


class Queue_Controller::Remove_Newest: public Algo
{
    public:
            
            Remove_Newest(Queue_Controller& qc): Algo(qc.mq_, qc.max_size_, qc.mq_size_) {}
            Result process_queue(size_t current_size);
};

namespace fplog
{
    class Priority_Filter;
};

class Queue_Controller::Remove_Newest_Below_Priority: public Algo
{
    public:
        
            Remove_Newest_Below_Priority(Queue_Controller& qc, const char* prio, bool inclusive = false):
            Algo(qc.mq_, qc.max_size_, qc.mq_size_), prio_(prio), inclusive_(inclusive) { make_filter(); }
            //~Remove_Newest_Below_Priority(){}
            
            Result process_queue(size_t current_size);


    private:

            Remove_Newest_Below_Priority();
            void make_filter();

            std::string prio_;
            bool inclusive_;
            
            std::shared_ptr<fplog::Priority_Filter> filter_;
};

class Queue_Controller::Remove_Oldest_Below_Priority: public Algo
{
    public:
        
            Remove_Oldest_Below_Priority(Queue_Controller& qc, const char* prio, bool inclusive = false):
            Algo(qc.mq_, qc.max_size_, qc.mq_size_), prio_(prio), inclusive_(inclusive) { make_filter(); }
            //~Remove_Oldest_Below_Priority(){}
            
            Result process_queue(size_t current_size);


    private:

            Remove_Oldest_Below_Priority();
            void make_filter();

            std::string prio_;
            bool inclusive_;
            
            std::shared_ptr<fplog::Priority_Filter> filter_;
};
