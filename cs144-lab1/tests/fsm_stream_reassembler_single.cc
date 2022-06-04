#include "byte_stream.hh"
#include "fsm_stream_reassembler_harness.hh"
#include "stream_reassembler.hh"
#include "util.hh"

#include <exception>
#include <iostream>

using namespace std;

int main() {
    try {
        {
            ReassemblerTestHarness test{8};

            test.execute(SubmitSegment{"abc", 0});
            test.execute(BytesAssembled(3));
            test.execute(NotAtEof{});
            cout << "ok1" << endl;

            test.execute(SubmitSegment{"ghX", 6}.with_eof(true));
            test.execute(BytesAssembled(3));
            test.execute(NotAtEof{});
            cout << "ok2" << endl;

            test.execute(SubmitSegment{"cdefg", 2});
            test.execute(BytesAssembled(8));
            test.execute(BytesAvailable{"abcdefgh"});
            test.execute(NotAtEof{});
            cout << "ok3" << endl;
        }
        {
            ReassemblerTestHarness test{65000};

            test.execute(BytesAssembled(0));
            test.execute(BytesAvailable(""));
            test.execute(NotAtEof{});
            cout << "ok4" << endl;
        }

        {
            ReassemblerTestHarness test{65000};

            test.execute(SubmitSegment{"a", 0});

            test.execute(BytesAssembled(1));
            test.execute(BytesAvailable("a"));
            test.execute(NotAtEof{});
            cout << "ok5" << endl;
        }

        {
            ReassemblerTestHarness test{65000};

            test.execute(SubmitSegment{"a", 0}.with_eof(true));

            test.execute(BytesAssembled(1));
            test.execute(BytesAvailable("a"));
            test.execute(AtEof{});
            cout << "ok6" << endl;
        }

        {
            ReassemblerTestHarness test{65000};

            test.execute(SubmitSegment{"", 0}.with_eof(true));

            test.execute(BytesAssembled(0));
            test.execute(BytesAvailable(""));
            test.execute(AtEof{});
            cout << "ok7" << endl;
        }

        {
            ReassemblerTestHarness test{65000};

            test.execute(SubmitSegment{"b", 0}.with_eof(true));

            test.execute(BytesAssembled(1));
            test.execute(BytesAvailable("b"));
            test.execute(AtEof{});
            cout << "ok8" << endl;
        }

        {
            ReassemblerTestHarness test{65000};

            test.execute(SubmitSegment{"", 0});

            test.execute(BytesAssembled(0));
            test.execute(BytesAvailable(""));
            test.execute(NotAtEof{});
            cout << "ok9" << endl;
        }
    } catch (const exception &e) {
        cerr << "Exception: " << e.what() << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
