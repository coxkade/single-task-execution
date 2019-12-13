build_dir?=build

.PHONY: mac_config config rm_build all clean

all: $(build_dir)
	make -C $(build_dir) $@

clean: 
	make -C $(build_dir) $@
	
mac_config: $(build_dir)
	cmake -GXcode -S . -B $(build_dir)

config: $(build_dir)
	cmake -S . -B $(build_dir)

rm_build: $(build_dir)
	$(RM) -rf $(build_dir)
	
$(build_dir):
	mkdir $@