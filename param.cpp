#include "param.h"
#include <cmath>
#include<cstring>
#include<string>

int totalnumRealblock = 2000000;
int OramL = static_cast<int>(ceil(log2(totalnumRealblock)));
int numLeaves = 1 << OramL;
int capacity=(1 << (OramL + 1)) - 1;
int blocksize = 4096;
int realBlockEachbkt = 4;
int dummyBlockEachbkt = 6;
int k=1;
int EvictRound = 20;
std::string dataname = "data/data_262144.txt";
std::string queryname = "data/query_3keywords.txt";
block dummyBlock(-1, -1, {});
int maxblockEachbkt = realBlockEachbkt + dummyBlockEachbkt;

int cacheLevel = (OramL/2);
int nodes_load=2;