import ctypes
import os
import datetime
from enum import IntEnum

class cBenchCounters(ctypes.Structure):
    '''
    This has to match the returned struct in library.c
    '''
    _fields_ = [ ("failed_adds", ctypes.c_int),
                 ("successful_adds", ctypes.c_int),
                 ("failed_removes", ctypes.c_int),
                 ("succesfull_removes", ctypes.c_int),
                 ("failed_contains", ctypes.c_int),
                 ("succesfull_contains", ctypes.c_int) ]

class cBenchResult(ctypes.Structure):
    '''
    This has to match the returned struct in library.c
    '''
    _fields_ = [ ("time", ctypes.c_float),
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
    DISJOINT = 1,
    PER_THREAD = 2

class cSelectionStrategy(CtypesEnum):
    RANDOM = 0,
    UNIQUE = 1,
    SUCCESSIVE = 2


class Benchmark:
    '''
    Class representing a benchmark. It assumes any benchmark sweeps over some
    parameter xrange using the fixed set of inputs for every point. It simply
    averages the results over the given amount of repetitions.
    '''
    def __init__(self, bench_function, parameters,
                 threads, repetitions_per_point, basedir, name):
        self.bench_function = bench_function
        self.parameters = parameters
        self.threads = threads
        self.repetitions_per_point = repetitions_per_point
        self.basedir = basedir
        self.name = name

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
        for x in self.threads:
            for r in range(0, self.repetitions_per_point):
                paras = self.parameters
                result = self.bench_function(*paras)
                tmp.append( result )
            self.data[x] = tmp

    def write_avg_data(self):
        '''
        Writes averages for each point measured into a dataset in the data
        folder timestamped when the run was started.
        '''
        if self.now is None:
            raise Exception("Benchmark was not run. Run before writing data.")

        try:
            os.makedirs(f"{self.basedir}/data/{self.now}/avg")
        except FileExistsError:
            pass
        with open(f"{self.basedir}/data/{self.now}/avg/{self.name}.data", "w")\
                as datafile:
            datafile.write(f"n_threads datapoint\n")
            for x, box in self.data.items():
                
                datafile.write(f"{x} {sum(box)/len(box)}\n")

def benchmark():
    '''
    Requires the binary to also be present as a shared library.
    '''
    basedir = os.path.dirname(os.path.abspath(__file__))
    seq_binary = ctypes.CDLL( f"{basedir}/build/seq_skiplist.so" )
    # Set the types for each benchmark function
    seq_binary.seq_skiplist_benchmark.argtypes = [ctypes.c_uint16, ctypes.c_uint16, 
        cOperationsMix, cSelectionStrategy, ctypes.c_uint, cKeyrange, ctypes.c_uint8, ctypes.c_double ]
    seq_binary.seq_skiplist_benchmark.restype = ctypes.POINTER(cBenchResult)

    # The number of threads. This is the x-axis in the benchmark, i.e., the
    # parameter that is 'sweeped' over.
    num_threads = [1,2,4,8,10,20,64]#,128,256]
    repetitions = 3

    time = ctypes.c_uint16(1)
    prefill = ctypes.c_uint16(1000)
    op_mix = cOperationsMix(0.1, 0.8)
    strat = cSelectionStrategy(cSelectionStrategy.UNIQUE)
    seed = ctypes.c_uint(12345)
    keyrange = cKeyrange(0, 100000)
    levels = ctypes.c_uint8(4)
    prob = ctypes.c_double(0.5)

    # Parameters for the benchmark are passed in a tuple, here (1000,). To pass
    # just one parameter, we cannot write (1000) because that would not parse
    # as a tuple, instead python understands a trailing comma as a tuple with
    # just one entry.
    sequential = Benchmark(seq_binary.seq_skiplist_benchmark, 
            (time, prefill, op_mix, strat, seed, keyrange, levels, prob),
            [1], repetitions, basedir, "sequential")

    #smallbench_100 = Benchmark(binary.small_bench, (100,), 3,
    #                           num_threads, basedir, "smallbench_100")

    sequential.run()
    sequential.write_avg_data()
    #smallbench_100.run()
    #smallbench_100.write_avg_data()

if __name__ == "__main__":
    benchmark()
