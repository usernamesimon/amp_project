CC ?= gcc
RM ?= @rm
MKDIR ?= @mkdir
PYTHON ?= @python3

CFLAGS := -O3 -Wall -Wextra -fopenmp

SRC_DIR = src
BUILD_DIR = build
DATA_DIR = data
INCLUDES = inc

SOURCES = benchmark.c seq_skiplist.c coarse_skiplist.c
NAME = $(SOURCES:%.c=%)
OBJECTS= $(SOURCES:%.c=%.o)

all: seq_skiplist

deb: 
	@echo $(OBJECTS)

$(DATA_DIR):
	@echo "Creating data directory: $(DATA_DIR)"
	$(MKDIR) $(DATA_DIR)

$(BUILD_DIR):
	@echo "Creating build directory: $(BUILD_DIR)"
	$(MKDIR) $(BUILD_DIR)

benchmark.o: $(SRC_DIR)/benchmark.c
	@echo "Compiling $<"
	$(CC) -O3 -Wall -Wextra -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

benchmark.so: $(OBJECTS)
	@echo "Linking $@"
	$(CC) -O3 -Wall -Wextra -fPIC -shared -o $(BUILD_DIR)/$@ $(OBJECTS:%=$(BUILD_DIR)/%) 

seq_skiplist.o: $(SRC_DIR)/seq_skiplist.c
	@echo "Compiling $<"
	$(CC) -O3 -Wall -Wextra -nostartfiles -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

# seq_skiplist: seq_skiplist.o
# 	@echo "Linking $@"
# 	$(CC) -O3 -Wall -Wextra -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^

seq_skiplist.so: seq_skiplist.o
	@echo "Linking $@"
	$(CC) -O3 -Wall -Wextra -fPIC -shared -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^ 

coarse_skiplist.o: $(SRC_DIR)/coarse_skiplist.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c $< -o $(BUILD_DIR)/$@

# coarse_skiplist: coarse_skiplist.o
# 	@echo "Linking $@"
# 	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^

coarse_skiplist.so: coarse_skiplist.o
	@echo "Linking $@"
	$(CC) $(CFLAGS) -fPIC -shared -o $(BUILD_DIR)/$@ $(BUILD_DIR)/$^ 

bench:
	@echo "This could run a sophisticated benchmark"

small-bench: $(BUILD_DIR) $(DATA_DIR) benchmark.so
	@echo "Running small-bench ..."
	$(PYTHON) benchmark.py

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
	@zip project.zip benchmark.py Makefile README src/* plots/avg_plot.tex report/report.tex run_nebula.sh

clean:
	@echo "Cleaning build directory: $(BUILD_DIR) and binaries: $(NAME) $(NAME).so"
	$(RM) -Rf $(BUILD_DIR)
	$(RM) -f $(NAME) $(NAME).so

.PHONY: all clean report
