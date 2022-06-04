#include <fstream>
#include <iostream>
using namespace std; 
void _log_print(const std::string &_string);

int
main(void){
    _log_print("cs144");
    return 0;
}

void
_log_print(const std::string &_string) {
    ofstream ofs;
    ofs.open("log", ios::app);
    ofs << "start" << endl;
    ofs << _string << endl;
    ofs << "end" << endl;
    ofs.close();
}