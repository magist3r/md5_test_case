// md5calc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <fstream>
#include <exception>

#include <string>
#include <queue>
#include <vector>
#include <map>
#include <thread>

#include <mutex>
#include <condition_variable>
#include "openssl/md5.h"

using namespace std;

class hash_calc
{
public:
	hash_calc(size_t block_size = 0x100000, unsigned int thread_count = thread::hardware_concurrency()) : m_block_size(block_size)
	{
		if (thread_count < 1) thread_count = 1; // Create at least one worker thread
		
		for (unsigned int i = 0; i < thread_count; ++i)
			m_threads.emplace_back(&hash_calc::run, this);
	}
	void run()
	{
		try
		{
			unique_lock<mutex> lock(m_mutex);

			while (true)
			{
				if (!m_queue.empty())
				{
					auto& pair = m_queue.front();
					if (pair.first == -1 && !pair.second) // exit condition for terminating
					{
						m_cv.notify_one();
						break;
					}

					if (!pair.second)
						throw runtime_error("Got empty block # " + to_string(pair.first));

					// save data for calculation and remove from queue
					int block_number = pair.first;
					auto block = std::move(pair.second);
					m_queue.pop();
					
					// calc md5
					lock.unlock();
					auto digest = make_unique<uint8_t[]>(MD5_DIGEST_LENGTH);
					MD5(block.get(), m_block_size, digest.get());
					lock.lock();

					m_digests.emplace(block_number, std::move(digest));
				}

				m_cv.wait(lock, [=] { return !m_queue.empty(); });
			}
		}
		catch (exception& e)
		{
			cerr << "Thread #" << this_thread::get_id() << " failed with error: " << e.what() << endl;
		}
	}
	void add_block(pair<int, unique_ptr<uint8_t[]>> pair)
	{
		{
			lock_guard<mutex> lock(m_mutex);
			m_queue.push(std::move(pair));
		}
		m_cv.notify_all();
	}
	void join()
	{
		for (thread& th : m_threads)
			th.join();
	}
	void finish()
	{
		{
			lock_guard<mutex> lock(m_mutex);
			m_queue.emplace(-1, nullptr); // exit condition
		}
		m_cv.notify_all();
		join();
	}
	auto & result() const
	{
		return m_digests;
	}

private:
	mutex m_mutex;
	condition_variable m_cv;
	vector<thread> m_threads;

	// data
	queue < pair<int /*block number*/, unique_ptr<uint8_t[]> /*block data*/> > m_queue;
	size_t m_block_size = 0x100000; // 1MB
	map<int /*block number*/, unique_ptr<uint8_t[]> /*digest*/> m_digests;
};

void print_usage(const string & name)
{
	cout << "Usage: " << name << " -i input_file -o output_file [-bs block_size] (in bytes)" << endl;
}

int main(int argc, char** argv)
{
	int result = 0;

	try {

		if (argc < 2)
			throw invalid_argument("No command line argument passed!");

		// input
		string input_name;
		string output_name;
		size_t block_size = 0x100000; // 1MB

		for (int i = 0; i < argc; ++i)
		{
			if (i == 0) continue;
			if (i % 2 == 1) // option
			{
				string par = argv[i];
				if ("-i" == par)
				{
					if (i + 1 >= argc)
						throw invalid_argument("Input file parameter not specified!");

					input_name = argv[i + 1];
				}
				else if ("-o" == par)
				{
					if (i + 1 >= argc)
						throw invalid_argument("Output file parameter not specified!");

					output_name = argv[i + 1];
				}
				else if ("-bs" == par)
				{
					if (i + 1 >= argc)
						throw invalid_argument("Block size parameter not specified!");

					block_size = stoi(argv[i + 1]);
				}
				else
					throw invalid_argument(string("Invalid argument ") + argv[i]);
			}
		}

		ifstream input(input_name, ios::binary);

		if (!input)
			throw runtime_error("Error opening input file: " + input_name + ", error code: " + to_string(errno));

		ofstream output(output_name, ios::binary);
		if (!output)
			throw runtime_error("Error opening output file: " + output_name + ", error code: " + to_string(errno));

		hash_calc calc(block_size);
		int block_number = 0;
		
		while (input)
		{
			auto buffer = make_unique<uint8_t[]>(block_size); // zero-initializes buffer
			input.read(reinterpret_cast<char *>(buffer.get()), block_size);
			
			calc.add_block({ block_number++, std::move(buffer) });
		}

		calc.finish();

		vector<uint8_t> res;
		auto & map = calc.result();
		for (auto it = map.begin(); it != map.end(); ++it)
			res.insert(res.end(), it->second.get(), it->second.get() + MD5_DIGEST_LENGTH);
			
		output.write(reinterpret_cast<char*>(res.data()), res.size());
	}
	catch (const std::invalid_argument& e)
	{
		cerr << e.what() << endl;
		print_usage(argv[0]);
		result = 1;
	}
	catch (const std::exception& e) 
	{
		cerr << e.what() << endl;
		result = 2;
	}

	return result;
}
