import ctypes
import os
import datetime
from enum import IntEnum

class cBenchCounters(ctypes.Structure):
    '''
    This has to match the returned struct in library.c
    '''
    _fields_ = [ ("failed_adds", ctypes.c_int),
                 ("successfull_adds", ctypes.c_int),
                 ("failed_removes", ctypes.c_int),
                 ("successfull_removes", ctypes.c_int),
                 ("failed_contains", ctypes.c_int),
                 ("successfull_contains", ctypes.c_int) ]

class cBenchResult(ctypes.Structure):
    '''
    This has to match the returned struct in library.c
    '''
    _fields_ = [ ("cpu_time", ctypes.c_float),
                 ("counters", cBenchCounters) ]
    
class cOperationsMix(ctypes.Structure):
    _fields_ = [ ("insert_p", ctypes.c_float),
                 ("contains_p", ctypes.c_float) ]
    
class cKeyrange(ctypes.Structure):
    _fields_ = [ ("min", ctypes.c_int),
                 ("max", ctypes.c_int) ]
    
class CtypesEnum(IntEnum):
    """A ctypes-compatible IntEnum superclass."""
    @classmethod
    def from_param(cls, obj):
        return int(obj)
    
class cKeyOverlap(CtypesEnum):
    COMMON = 0,
    DISJOINT = 1

class cSelectionStrategy(CtypesEnum):
    RANDOM = 0,
    UNIQUE = 1,
    SUCCESSIVE = 2

class cImplementation(CtypesEnum):
    SEQUENTIAL = 0,
    COARSE = 1,
    FINE = 2,
    LOCK_FREE = 3


class Benchmark:
    '''
    Class representing a benchmark. It assumes any benchmark sweeps over some
    parameter xrange using the fixed set of inputs for every point. It simply
    averages the results over the given amount of repetitions.
    '''
    def __init__(self, binary, parameters,
                 threads, repetitions_per_point, basedir, graph_name):
        self.binary = binary
        self.parameters = parameters
        self.seq_parameters = tuple(x for x in parameters if not(type(x) is cKeyOverlap))
        self.threads = threads
        self.repetitions_per_point = repetitions_per_point
        self.basedir = basedir
        self.graph_name = graph_name

        self.data = {}
        self.now = None

    def run(self):
        '''
        Runs the benchmark with the given parameters. Collects
        repetitions_per_point data points and writes them back to the data
        dictionary to be processed later.
        '''
        self.now = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        print(f"Starting Benchmark run at {self.now}")

        tmp = []
        print("SEQUENTIAL", end=" ", flush=True)
        for r in range(0, self.repetitions_per_point):
            result = self.binary.seq_skiplist_benchmark(*self.seq_parameters)
            tmp.append( result )
            print(".", end=" ", flush=True)
        self.data[1] = tmp
        self.write_avg_data("SEQUENTIAL")
        self.data.clear()
        print()
    
        for impl in [cImplementation.COARSE, cImplementation.LOCK_FREE]:
            print(f"{impl.name}", end=" ", flush=True)
            for x in self.threads:
                tmp.clear()
                for r in range(0, self.repetitions_per_point):
                    result = self.binary.parallel_skiplist_benchmark(ctypes.c_uint16(x), *self.parameters, impl)
                    tmp.append( result )
                    print(".", end=" ", flush=True)
                self.data[x] = tmp.copy()
            self.write_avg_data(impl.name)
            self.data.clear()
            print()
        
        end_time = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        print(f"Finished Benchmark run at {end_time}")

    def write_avg_data(self, filename):
        '''
        Writes averages for each point measured into a dataset in the data
        folder timestamped when the run was started.
        '''
        if self.now is None:
            raise Exception("Benchmark was not run. Run before writing data.")

        try:
            os.makedirs(f"{self.basedir}/data/{self.now}/{self.graph_name}")
        except FileExistsError:
            pass
        with open(f"{self.basedir}/data/{self.now}/{self.graph_name}/{filename}.data", "w")\
                as datafile:
            datafile.write(f"n_threads succesfull_adds failed_adds succesfull_contains "
                           "failed_contains successfull_removes failed_removes "
                           "total_operations processor_time throughput\n")
            for x, box in self.data.items():
                
                times = [p.contents.cpu_time for p in box]
                avg_time = sum(times)/len(times)

                s_adds = [p.contents.counters.successfull_adds for p in box]
                avg_s_adds = sum(s_adds)/len(s_adds)
                f_adds = [p.contents.counters.failed_adds for p in box]
                avg_f_adds = sum(f_adds)/len(f_adds)

                s_removes = [p.contents.counters.successfull_removes for p in box]
                avg_s_removes = sum(s_removes)/len(s_removes)
                f_removes = [p.contents.counters.failed_removes for p in box]
                avg_f_removes = sum(f_removes)/len(f_removes)

                s_contains = [p.contents.counters.successfull_adds for p in box]
                avg_s_contains = sum(s_contains)/len(s_contains)
                f_contains = [p.contents.counters.failed_adds for p in box]
                avg_f_contains = sum(f_contains)/len(f_contains)

                total_ops = [s_a+f_a+s_r+f_r+s_c+f_c for s_a,f_a,s_r,f_r,s_c,f_c in
                             zip(s_adds, f_adds, s_removes, f_removes, s_contains, f_contains)]
                avg_total_ops = sum(total_ops)/len(total_ops)

                avg_throughput = sum(total_ops)/sum(times)
                
                datafile.write(f"{x} {avg_s_adds} {avg_f_adds} {avg_s_contains} "
                               f"{avg_f_contains} {avg_s_removes} {avg_f_removes} "
                               f"{avg_total_ops} {avg_time} {avg_throughput}\n")

def benchmark():
    '''
    Requires the binary to also be present as a shared library.
    '''
    basedir = os.path.dirname(os.path.abspath(__file__))
    benchmark_binary = ctypes.CDLL( f"{basedir}/build/benchmark.so" )
    # Set the types for each benchmark function
    benchmark_binary.seq_skiplist_benchmark.argtypes = [ctypes.c_uint16, ctypes.c_uint16, 
        cOperationsMix, cSelectionStrategy, ctypes.c_uint, cKeyrange, ctypes.c_uint8, ctypes.c_double ]
    benchmark_binary.seq_skiplist_benchmark.restype = ctypes.POINTER(cBenchResult)

    benchmark_binary.parallel_skiplist_benchmark.argtypes = \
    [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint16, cOperationsMix, cSelectionStrategy, cKeyOverlap,
    ctypes.c_uint, cKeyrange, ctypes.c_uint8, ctypes.c_double, cImplementation]
    benchmark_binary.parallel_skiplist_benchmark.restype = ctypes.POINTER(cBenchResult)

    # The number of threads. This is the x-axis in the benchmark, i.e., the
    # parameter that is 'sweeped' over.
    num_threads = [1,2,4,8,10,20,64]#,128,256]
    repetitions = 2

    time = ctypes.c_uint16(1)
    prefill = ctypes.c_uint16(10000)
    op_mix = cOperationsMix(0.1, 0.8)
    strat = cSelectionStrategy(cSelectionStrategy.UNIQUE)
    overlap = cKeyOverlap(cKeyOverlap.COMMON)
    seed = ctypes.c_uint(12345)
    keyrange = cKeyrange(0, 100000)
    levels = ctypes.c_uint8(4)
    prob = ctypes.c_double(0.5)
    impl = [cImplementation(cImplementation.COARSE)]

    # Parameters for the benchmark are passed in a tuple, here (1000,). To pass
    # just one parameter, we cannot write (1000) because that would not parse
    # as a tuple, instead python understands a trailing comma as a tuple with
    # just one entry.
    sequential = Benchmark(benchmark_binary, 
            (time, prefill, op_mix, strat, seed, keyrange, levels, prob),
            [1], repetitions, basedir, "sequential")
    
    coarse = Benchmark(benchmark_binary,
            (time, prefill, op_mix, strat, overlap, seed, keyrange, levels, prob),
            num_threads, repetitions, basedir, "small-bench")

    #sequential.run()
    #sequential.write_avg_data()
    coarse.run()
    #coarse.write_avg_data()
    

if __name__ == "__main__":
    benchmark()
