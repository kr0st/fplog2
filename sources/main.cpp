#include <iostream>
#include <date/date.h>
#include <fplog_exceptions.h>
#include <sprot.h>
#include <utils.h>

using namespace std;

int main()
{
    std::vector<std::any> any_vector;
    any_vector.push_back(32);
    any_vector.push_back(std::string(" thirty two"));

    cout << "Hello World! " << std::any_cast<int>(any_vector[0]) << std::any_cast<std::string>(any_vector[1]) << endl;
    return 0;
}
