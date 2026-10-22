// Standalone entry point; the game menu uses the identical shared runner.
#define main vurusUnusedApplicationMain
#include "main.cpp"
#undef main

int main(int argc, char** argv) {
    return runBenchmark(argc, argv);
}
