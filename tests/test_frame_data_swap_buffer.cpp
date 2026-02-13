// FrameData 与 SwapBuffer 单元测试

#include <kale_executor/frame_data.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_CHECK(cond)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " " << #cond \
                      << std::endl;                                             \
            std::exit(1);                                                       \
        }                                                                      \
    } while (0)

static void test_swap_buffer_double_basic() {
    kale::executor::SwapBuffer<int, 2> sb;
    sb.current_for_writer() = 42;
    TEST_CHECK(sb.current_for_writer() == 42);
    // 未 swap 前读者看到的是另一槽（初始值 0）
    TEST_CHECK(sb.current_for_reader() == 0);
    sb.swap();
    TEST_CHECK(sb.current_for_reader() == 42);
    sb.current_for_writer() = 100;
    TEST_CHECK(sb.current_for_reader() == 42);
    sb.swap();
    TEST_CHECK(sb.current_for_reader() == 100);
}

static void test_swap_buffer_triple() {
    kale::executor::SwapBuffer<int, 3> sb;
    sb.current_for_writer() = 1;
    sb.swap();
    TEST_CHECK(sb.current_for_reader() == 1);
    sb.current_for_writer() = 2;
    sb.swap();
    TEST_CHECK(sb.current_for_reader() == 2);
    sb.current_for_writer() = 3;
    sb.swap();
    TEST_CHECK(sb.current_for_reader() == 3);
}

static void test_frame_data_basic() {
    kale::executor::FrameData<std::vector<int>> fd;
    fd.write_buffer().push_back(10);
    fd.write_buffer().push_back(20);
    TEST_CHECK(fd.write_buffer().size() == 2u);
    // 未 end_frame 时 read_buffer 是另一缓冲（空）
    TEST_CHECK(fd.read_buffer().empty());
    fd.end_frame();
    TEST_CHECK(fd.read_buffer().size() == 2u);
    TEST_CHECK(fd.read_buffer()[0] == 10 && fd.read_buffer()[1] == 20);
    fd.write_buffer().clear();
    fd.write_buffer().push_back(30);
    TEST_CHECK(fd.read_buffer().size() == 2u); // 读者仍见上一帧
    fd.end_frame();
    TEST_CHECK(fd.read_buffer().size() == 1u && fd.read_buffer()[0] == 30);
}

static void test_frame_data_multiple_end_frame() {
    kale::executor::FrameData<int> fd;
    fd.write_buffer() = 1;
    fd.end_frame();
    TEST_CHECK(fd.read_buffer() == 1);
    fd.write_buffer() = 2;
    fd.end_frame();
    TEST_CHECK(fd.read_buffer() == 2);
    fd.write_buffer() = 3;
    fd.end_frame();
    TEST_CHECK(fd.read_buffer() == 3);
}

static void test_frame_data_triple_buffering() {
    kale::executor::FrameDataTriple<int> fd;
    fd.write_buffer() = 1;
    fd.end_frame();
    TEST_CHECK(fd.read_buffer() == 1);
    fd.write_buffer() = 2;
    fd.end_frame();
    TEST_CHECK(fd.read_buffer() == 2);
    fd.write_buffer() = 3;
    fd.end_frame();
    TEST_CHECK(fd.read_buffer() == 3);
}

int main() {
    test_swap_buffer_double_basic();
    test_swap_buffer_triple();
    test_frame_data_basic();
    test_frame_data_multiple_end_frame();
    test_frame_data_triple_buffering();
    std::cout << "test_frame_data_swap_buffer: all passed\n";
    return 0;
}
