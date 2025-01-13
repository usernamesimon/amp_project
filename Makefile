CC ?= gcc
RM ?= @rm
MKDIR ?= @mkdir
PYTHON ?= @python3

CFLAGS := -O3 -Wall -Wextra -fopenmp

SRC_DIR = src
BUILD_DIR = build
DATA_DIR = data
INCLUDES = inc

SOURCES = benchmark.c seq_skiplist.c coarse_skiplist.c fine_skiplist.c lock_free_skiplist.c
NAME = $(SOURCES:%.c=%)
OBJECTS= $(SOURCES:%.c=%.o)
D_OBJECTS = $(SOURCES:%.c=%_debug.o)

all: $(BUILD_DIR) benchmark.so

$(DATA_DIR):
	@echo "Creating data directory: $(DATA_DIR)"
	$(MKDIR) $(DATA_DIR)

$(BUILD_DIR):
	@echo "Creating build directory: $(BUILD_DIR)"
	$(MKDIR) $(BUILD_DIR)

benchmark.o: $(SRC_DIR)/benchmark.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

benchmark_debug.o: $(SRC_DIR)/benchmark.c
	@echo "Compiling $<"
	$(CC) -fopenmp -Wall -Wextra -g -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

benchmark.so: $(OBJECTS)
	@echo "Linking $@"
	$(CC) $(CFLAGS) -fPIC -shared -o $(BUILD_DIR)/$@ $(OBJECTS:%=$(BUILD_DIR)/%) 

benchmark_debug: $(D_OBJECTS)
	@echo "Linking $@"
	$(CC) -g -fopenmp -Wall -Wextra -fPIC -o $(BUILD_DIR)/$@ $(D_OBJECTS:%=$(BUILD_DIR)/%) 

seq_skiplist.o: $(SRC_DIR)/seq_skiplist.c
	@echo "Compiling $<"
	$(CC) -O3 -Wall -Wextra -nostartfiles -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

seq_skiplist_debug.o: $(SRC_DIR)/seq_skiplist.c
	@echo "Compiling $<"
	$(CC) -g -Wall -Wextra -nostartfiles -DDEBUG -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

# seq_skiplist: seq_skiplist.o
# 	@echo "Linking $@"
# 	$(CC) -O3 -Wall -Wextra -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^

seq_skiplist.so: seq_skiplist.o
	@echo "Linking $@"
	$(CC) -O3 -Wall -Wextra -fPIC -shared -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^ 

coarse_skiplist.o: $(SRC_DIR)/coarse_skiplist.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

coarse_skiplist_debug.o: $(SRC_DIR)/coarse_skiplist.c
	@echo "Compiling $<"
	$(CC) -fopenmp -Wall -Wextra -g -DDEBUG -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

# coarse_skiplist: coarse_skiplist.o
# 	@echo "Linking $@"
# 	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^

fine_skiplist.so: fine_skiplist.o
	@echo "Linking $@"
	$(CC) $(CFLAGS) -fPIC -shared -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^ 

fine_skiplist.o: $(SRC_DIR)/fine_skiplist.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

fine_skiplist_debug.o: $(SRC_DIR)/fine_skiplist.c
	@echo "Compiling $<"
	$(CC) -fopenmp -Wall -Wextra -g -DDEBUG -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

# fine_skiplist: fine_skiplist.o
# 	@echo "Linking $@"
# 	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^

lock_free_skiplist.so: lock_free_skiplist.o
	@echo "Linking $@"
	$(CC) $(CFLAGS) -fPIC -shared -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^ 

lock_free_skiplist.o: $(SRC_DIR)/lock_free_skiplist.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

lock_free_skiplist_debug.o: $(SRC_DIR)/lock_free_skiplist.c
	@echo "Compiling $<"
	$(CC) -fopenmp -Wall -Wextra -g -DDEBUG -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

# lock_free_skiplist: lock_free_skiplist.o
# 	@echo "Linking $@"
# 	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^

bench:
	@echo "Running big benchmark, this could take a few minutes..."
	$(PYTHON) benchmark.py

small-bench: $(BUILD_DIR) $(DATA_DIR) benchmark.so
	@echo "Running small-bench ..."
	$(PYTHON) small_benchmark.py

small-plot: 
	@echo "Plotting small-bench results ..."
	bash -c 'cd plots && pdflatex "\newcommand{\DATAPATH}{../data/$$(ls ../data/ | sort -r | head -n 1)}\input{avg_plot.tex}"'
	@echo "============================================"
	@echo "Created plots/avgplot.pdf"

report: small-plot
	@echo "Compiling report ..."
	bash -c 'cd report && pdflatex report.tex'
	@echo "============================================"
	@echo "Done"

zip:
	@zip project.zip benchmark.py small_benchmark.py Makefile README src/* inc/* plots/avg_plot.tex report/report.tex run_nebula.sh

clean:
	@echo "Cleaning build directory: $(BUILD_DIR) and binaries: $(NAME) $(NAME).so"
	$(RM) -Rf $(BUILD_DIR)
	$(RM) -f $(NAME) $(NAME).so

lock_free_skiplist_benchmark: $(SRC_DIR)/lock_free_skip_list_benchmark.c $(SRC_DIR)/lock_free_skiplist.c
	@echo "Compiling lock_free_skiplist_benchmark ..."
	$(CC) -o lock_free_skiplist_benchmark $(SRC_DIR)/lock_free_skip_list_benchmark.c $(SRC_DIR)/lock_free_skiplist.c -I$(INCLUDES) -pthread -O2


.PHONY: all clean report
