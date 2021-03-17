
#include <sys/ipc.h>
#include <sys/shm.h>

// standard C/C++ headers
#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <string>
#include <iostream>
#include <time.h>
#include <sys/time.h>
#include <cassert>

// rapidjson headers
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/writer.h"
using namespace rapidjson;

// opencl harness headers
#include "xcl2.hpp"
#include "ap_fixed.h"
#include "ap_int.h"
#include <cmath>
#include <vector>

#include <algorithm>

template<class RandomIterator>
void transpose(RandomIterator first, RandomIterator last, int m)
{
    const int mn1 = (last - first - 1);
    const int n   = (last - first) / m;
    std::vector<bool> visited(last - first);
    RandomIterator cycle = first;
    while (++cycle != last) {
        if (visited[cycle - first])
            continue;
        int a = cycle - first;
        do  {
            a = a == mn1 ? mn1 : (n * a) % mn1;
            std::swap(*(first + a), *cycle);
            visited[a] = true;
        } while ((first + a) != cycle);
    }
}

int main(int argc, char ** argv) {
  std::cout << "[INFO] Initialize input buffers...\n";

  FILE *f = fopen("inputs.json", "r");
  char readBuffer[65536];
  FileReadStream is(f, readBuffer, sizeof(readBuffer));

  Document document;
  document.ParseStream(is);
  fclose(f);
  assert(document.HasMember("A"));
  const Value& A_d = document["A"];
  assert(A_d.IsArray());
  std::vector<int, aligned_allocator<int>> A(1048576);
   for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      A[i1 + i0*1024] = (A_d[i1 + i0*1024].GetInt());
    }
  }

  assert(document.HasMember("B"));
  const Value& B_d = document["B"];
  assert(B_d.IsArray());
  std::vector<int, aligned_allocator<int>> B(1048576);
   for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      B[i1 + i0*1024] = (B_d[i1 + i0*1024].GetInt());
    }
  }

  assert(document.HasMember("Y0"));
  const Value& Y0_d = document["Y0"];
  assert(Y0_d.IsArray());
  std::vector<int, aligned_allocator<int>> Y0(1048576);
   for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      Y0[i1 + i0*1024] = (Y0_d[i1 + i0*1024].GetInt());
    }
  }

  std::cout << "[ INFO ] Initialize RTE...\n";

  if (argc != 2) {
      std::cout << "Usage: " << argv[0] << " <XCLBIN File>" << std::endl;
      return EXIT_FAILURE;
  }

  auto binaryFile = argv[1];
  cl_int err = CL_SUCCESS;

  // create binary file and program
  auto fileBuf = xcl::read_binary_file(binaryFile);
  cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};

  cl::Context context;
  cl::CommandQueue q;
  cl::Program program;
  auto devices = xcl::get_xil_devices();
  int valid_device = 0;

  for (unsigned int i = 0; i < devices.size(); i++) {
      auto device = devices[i];
      // Creating Context and Command Queue for selected Device
      context = cl::Context(device, NULL, NULL, NULL, &err);
      q = cl::CommandQueue(
          context, device, CL_QUEUE_PROFILING_ENABLE, &err);

      std::cout << "Trying to program device[" << i
                << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
      program = cl::Program(context, {device}, bins, NULL, &err);
      if (err != CL_SUCCESS) {
          std::cout << "Failed to program device[" << i
                    << "] with xclbin file!\n";
      } else {
          std::cout << "Device[" << i << "]: program successful!\n";
          valid_device++;
          break; // we break because we found a valid device
      }
  }
  if (valid_device == 0) {
      std::cout << "Failed to program any device found, exit!\n";
      exit(EXIT_FAILURE);
  }


  // Compute and kernel call from host
  (void)transpose(B.begin(), B.end(), 1024);

  cl::Kernel kernel(program, "test", &err);
  cl::Buffer buffer_Y0(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, sizeof(ap_int<32>)*1024*1024, Y0.data(), &err);
  cl::Buffer buffer_A(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, sizeof(ap_int<32>)*1024*1024, A.data(), &err);
  cl::Buffer buffer_B(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, sizeof(ap_int<32>)*1024*1024, B.data(), &err);

  // set device kernel buffer
  err = kernel.setArg(0, buffer_Y0);
  err = kernel.setArg(1, buffer_A);
  err = kernel.setArg(2, buffer_B);
  err = q.enqueueMigrateMemObjects({buffer_Y0, buffer_A, buffer_B}, 0/*from host*/);
  q.finish();

  // enqueue kernel function
  std::chrono::duration<double> kernel_time(0);
  auto kernel_start = std::chrono::high_resolution_clock::now();
  cl::Event event;
  err = q.enqueueTask(kernel, NULL, &event);

  err = q.finish();
  auto kernel_end = std::chrono::high_resolution_clock::now();
  kernel_time = std::chrono::duration<double>(kernel_end - kernel_start);
  auto kernel_time_in_sec = kernel_time.count();
  std::cout << "Execution Time: " <<  kernel_time_in_sec << std::endl;
  err = q.enqueueMigrateMemObjects({buffer_Y0, buffer_A, buffer_B}, CL_MIGRATE_MEM_OBJECT_HOST);
  q.finish();

  // Execution on host 

  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  document["Y0"].Clear();
  rapidjson::Value v_Y0(rapidjson::kArrayType);
  for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      v_Y0.PushBack(rapidjson::Value().SetInt(Y0[i1 + i0*1024]), allocator);
    }
  }
  document["Y0"] = v_Y0;
  std::cout << "[ INFO ] Finish running...\n";

  FILE* fp = fopen("inputs.json", "w"); 
 
  char writeBuffer[65536];
  FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
 
  Writer<FileWriteStream> writer(os);
  document.Accept(writer);
  fclose(fp);

  

  }
