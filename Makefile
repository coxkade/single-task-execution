build_dir?=build

.PHONY: mac_config config rm_build all clean coverage reconfig test

all: $(build_dir)
	make -C $(build_dir) $@

clean: 
	make -C $(build_dir) $@
	
coverage:
	make clean
	make all
	make -C $(build_dir) $@
	
test:
	make clean
	make all
	make -C $(build_dir) $@
	
mac_config: $(build_dir)
	cmake -GXcode -S . -B $(build_dir)

config: $(build_dir)
	cmake -S . -B $(build_dir)

rm_build: $(build_dir)
	$(RM) -rf $(build_dir)

reconfig:
	make rm_build
	make config	

$(build_dir):
	mkdir $@