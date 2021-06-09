
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

#include <algorithm>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <ctime>
#include <cmath>
#include <chrono>

#define NUM_KERNEL 1
#define READ_PORT_NUM 1
#define WRITE_PORT_NUM 0
#define WIDTH_FACTOR 512/32

#include "CL/opencl.h"
#pragma message ("* Compiling for ALTERA CL")
#define AOCX_FILE "mykernel.aocx"

#define TEST
#define FPGA_DEF_PLACEHOLDER

#define CHECK(status) 							                              \
    if (status != CL_SUCCESS)						                          \
{									                                                \
    fprintf(stderr, "error %d in line %d.\n", status, __LINE__);	\
    exit(1);								                                      \
}									                                                \

void* acl_aligned_malloc (size_t size) {
  void *result = NULL;
  posix_memalign (&result, 64, size);
  return result;
}

double compute_kernel_execution_time(cl_event &event, double &start_d, double &end_d)
{
    cl_ulong start, end;
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, NULL);
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
    start_d = (double)1.0e-9 * start;
    end_d   = (double)1.0e-9 * end;
    return 	(double)1.0e-9 * (end - start); // nanoseconds to seconds
}


int main(int argc, char ** argv) {
  std::cout << "[INFO] Initialize input buffers...\n";

  auto placeholder2 = new float[1024][1024];
  for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      placeholder2[i0][i1] = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/10)); // (placeholder2_d[i1 + i0*1024].GetFloat());
    }
  }

  auto placeholder3 = new float[1024][1024];
  for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      placeholder3[i0][i1] = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/10)); //(placeholder3_d[i1 + i0*1024].GetFloat());
    }
  }

  auto out_matrix = new float[1024][1024];
  for (size_t i0 = 0; i0 < 1024; i0++) {
    for (size_t i1 = 0; i1 < 1024; i1++) {
      out_matrix[i0][i1] = 0; //(out_matrix_d[i1 + i0*1024].GetFloat());
    }
  }

  std::cout << "[ INFO ] Initialize RTE...\n";

  cl_int status;
  cl_uint numDevices = 0;
  cl_uint numPlatforms = 0;
  cl_platform_id* platforms = NULL;
  const cl_uint maxDevices = 4;
  cl_device_id devices[maxDevices];
  cl_event kernel_exec_event;

  // global and local worksize
  size_t globalWorkSize[1] = {1};
  size_t localWorkSize[1] = {1};

  // get platform and device information 
  status = clGetPlatformIDs(0, NULL, &numPlatforms);
  platforms = (cl_platform_id*) acl_aligned_malloc (numPlatforms * sizeof(cl_platform_id));
  status = clGetPlatformIDs(numPlatforms, platforms, NULL); CHECK(status);
  status = clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_ALL,
      maxDevices, devices, &numDevices); CHECK(status);

  // create contex and command queue 
  cl_context context = clCreateContext(NULL, 1, devices, NULL, NULL, &status);
  CHECK(status);
  cl_command_queue cmdQueue = clCreateCommandQueue(context, devices[0], 
      CL_QUEUE_PROFILING_ENABLE, &status);
  CHECK(status);

  // read aocx and create binary
  FILE *handle = fopen(AOCX_FILE, "rb");
  fseek(handle, 0, SEEK_END);
  size_t  binary_length = ftell(handle);

  // create program from binary 
  const unsigned char *binary;
  binary = (unsigned char*) malloc(sizeof(unsigned char) * binary_length);
  assert(binary && "Malloc failed"); rewind(handle);
  if (fread((void*)binary, binary_length, 1, handle) == 0) {
    printf("Failed to read from the AOCX file (fread).\n");
    return -1;
  }
  fclose(handle);
  cl_program program = clCreateProgramWithBinary(context, 1, devices,
      &binary_length, (const unsigned char **)&binary, &status, NULL);

  status = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
  CHECK(status);

  // Compute and kernel call from host
  cl_kernel kernel = clCreateKernel(program, "mykernel", &status);

  // ----------------- loop thru -------------

    // Specify default payload size
    int dataSize(0);
    // From 256*4=1KB to 1024*1024*4(1MB)
    for (int payload(256); payload <= 262144 * 32 * 32; payload*=2){

        dataSize = payload;
        // Allocate host buffer
        int read_source[dataSize];
        std::generate(read_source, read_source + dataSize, std::rand);
        // std::vector<int> write_source(NUM_KERNEL*WRITE_PORT_NUM);
        int write_source[dataSize];

	    std::vector<cl_mem> source_in_buffer(NUM_KERNEL*READ_PORT_NUM);
	    std::vector<cl_mem> source_out_buffer(NUM_KERNEL*WRITE_PORT_NUM);
      cl_mem dummy_buffer;

      // These commands will allocate memory on the FPGA. The cl::Buffer objects can
      // be used to reference the memory locations on the device.
      // Creating Buffers
      double pcie_rd_sec = 0;
      std::chrono::duration<double> pcie_rd_time(0);
      auto pcie_rd_start = std::chrono::high_resolution_clock::now();
      for (int i = 0; i < NUM_KERNEL*READ_PORT_NUM; i++) {
          source_in_buffer[i] = 
            clCreateBuffer(context, CL_MEM_READ_ONLY, 
              sizeof(int) * dataSize, NULL, &status); CHECK(status);

          // Copy input data to Device Global Memory
          status = clEnqueueWriteBuffer(cmdQueue, source_in_buffer[i], 
            CL_TRUE, 0, sizeof(int) * dataSize, read_source, 0, 
              NULL, NULL); CHECK(status);
      }
      status = clFlush(cmdQueue); CHECK(status);
      status = clFinish(cmdQueue); CHECK(status);

      auto pcie_rd_end = std::chrono::high_resolution_clock::now();
      pcie_rd_time = std::chrono::duration<double>(pcie_rd_end - pcie_rd_start);
      pcie_rd_sec = pcie_rd_time.count();

    // Create OpenCL buffers for writing
		for (int i = 0; i < NUM_KERNEL*WRITE_PORT_NUM; i++) {
      source_out_buffer[i] = 
        clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
          sizeof(int) * dataSize, NULL, &status); CHECK(status);
		}
    status = clFlush(cmdQueue); CHECK(status);
    status = clFinish(cmdQueue); CHECK(status);

    cl_ulong8 dummy[1];
		dummy_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
        sizeof(cl_ulong8) * 1, NULL, &status); CHECK(status);

        // Start timer
        double kernel_time_in_sec = 0;
        std::chrono::duration<double> kernel_time(0);
        auto kernel_start = std::chrono::high_resolution_clock::now();

        // Setting the compute kernel arguments
        dataSize = dataSize / WIDTH_FACTOR;
        int i, j = 0;
        for (i = 0; i < NUM_KERNEL; i++) {
            for (j = 0; j < READ_PORT_NUM; j++) {
                status = clSetKernelArg(kernel, j, 
                  sizeof(cl_mem), (void*)&source_in_buffer[i*READ_PORT_NUM+j]); CHECK(status);
            }
            for (j = 0; j < WRITE_PORT_NUM; j++) {
                int index = READ_PORT_NUM + j;
                status = clSetKernelArg(kernel, index, 
                  sizeof(cl_mem), (void*)&source_out_buffer[i*WRITE_PORT_NUM+j]); CHECK(status);
            }
            j = READ_PORT_NUM + WRITE_PORT_NUM;
            // data accessing trip count
            status = clSetKernelArg(kernel, 1, 
              sizeof(cl_mem), (void*)&dummy_buffer); CHECK(status);

            // set up the dummy output port. the dummy read might be optimized away
            // the memory transaction's pipeline depth is 2
            unsigned int trip_count = dataSize;
            status = clSetKernelArg(kernel, 2, 
              sizeof(cl_uint), (void*)&trip_count); CHECK(status);
            // status = clEnqueueNDRangeKernel(cmdQueue, kernel, 1, NULL, globalWorkSize,  
            //   localWorkSize, 0, NULL, &kernel_exec_event); CHECK(status);
            status = clEnqueueTask(cmdQueue, kernel, 0, NULL, &kernel_exec_event); CHECK(status);
        }
        status = clFlush(cmdQueue); CHECK(status);
        status = clFinish(cmdQueue); CHECK(status);

        // Stop timer
        auto kernel_end = std::chrono::high_resolution_clock::now();
        kernel_time = std::chrono::duration<double>(kernel_end - kernel_start);
        kernel_time_in_sec = kernel_time.count();

        // Copying data back 
        double pcie_wr_sec = 0;
        std::chrono::duration<double> pcie_wr_time(0);
        auto pcie_wr_start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < NUM_KERNEL*WRITE_PORT_NUM; i++) {
          clEnqueueReadBuffer(cmdQueue, source_out_buffer[i], CL_TRUE, 0, 
            sizeof(int) * dataSize, write_source, 0, NULL, NULL);
        }
        status = clFlush(cmdQueue); CHECK(status);
        status = clFinish(cmdQueue); CHECK(status);

        auto pcie_wr_end = std::chrono::high_resolution_clock::now();
        pcie_wr_time = std::chrono::duration<double>(pcie_wr_end - pcie_wr_start);
        pcie_wr_sec = pcie_wr_time.count();

        std::cout << "----------------" << std::endl;
        std::cout << "Execution time = " << kernel_time_in_sec << "s. PCIe(RD) = " 
                  << pcie_rd_sec <<  "s. PCIe(WR) = " << pcie_wr_sec << "s" << std::endl;

        double rd_bw_result = payload * 4 / (1024*1024*1024*kernel_time_in_sec) * NUM_KERNEL * READ_PORT_NUM;
        double pcie_rd_bw_result = payload * 4 / (1024*1024*1024*pcie_rd_sec) * NUM_KERNEL * READ_PORT_NUM;
        std::cout << "Payload Size: " << payload*4/(1024.0*1024.0) << "MB - Bandwidth (RD) = " << rd_bw_result << "GB/s"
                  <<  ". PCIe(RD) = " << pcie_rd_bw_result << "GB/s" << std::endl;

        double wr_bw_result = payload * 4 / (1024*1024*1024*kernel_time_in_sec) * NUM_KERNEL * WRITE_PORT_NUM;
        double pcie_wr_bw_result = payload * 4 / (1024*1024*1024*pcie_wr_sec) * NUM_KERNEL * WRITE_PORT_NUM;
        std::cout << "Payload Size: " << payload*4/(1024.0*1024.0) << "MB - Bandwidth (WR) = " << wr_bw_result << "GB/s"
                  << ". PCIe(WR) = " << pcie_wr_bw_result << "GB/s" << std::endl;
        std::cout << "----------------" << std::endl;
    }
  // ----------------- end -------------------

  // execution on host 
  std::cout << "[ INFO ] Finish running...\n";

  double k_start_time;	
  double k_end_time;
  double k_exec_time;

  k_exec_time = compute_kernel_execution_time(kernel_exec_event, k_start_time, k_end_time);     
  printf("FPGA Execution time %.8f s \\n", k_exec_time);
  
}
