Usage: ./md5calc -i input_file -o output_file [-bs block_size] (in bytes)

Linux binary compilation string:
g++ -O3 -std=c++14 md5calc.cpp -I./ -o md5calc -lcrypto -lpthread