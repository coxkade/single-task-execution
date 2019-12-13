build_dir?=build

.PHONY: mac_config config rm_build

mac_config: $(build_dir)
	cmake -GXcode -S . -B $(build_dir)

config: $(build_dir)
	cmake -S . -B $(build_dir)

rm_build: $(build_dir)
	$(RM) -rf $(build_dir)
	
$(build_dir):
	mkdir $@