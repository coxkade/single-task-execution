build_dir?=build
mac_build_dir?=macbuild

.PHONY: mac_config config rm_build all clean coverage reconfig test semaphore-clean

all: $(build_dir)
	make -C $(build_dir) $@

clean: 
	make -C $(build_dir) $@
	
coverage: reconfig
	make all
	make -C $(build_dir) single-task-lib-coverage
	
test:
	make clean
	make all
	make -C $(build_dir) $@
	
mac_config: $(mac_build_dir)
	cmake -GXcode -S . -B $(mac_build_dir)

config: $(build_dir)
	cmake -S . -B $(build_dir)

rm_build: $(build_dir)
	$(RM) -rf $(build_dir)

reconfig: $(build_dir)
	make rm_build
	make config	
	
semaphore-clean: $(build_dir)
	make -C $(build_dir) $@

$(build_dir):
	mkdir $@

$(mac_build_dir):
	mkdir $@