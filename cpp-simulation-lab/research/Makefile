PROJECTS := transformer prm grpo specserve cache
.PHONY: all test clean $(PROJECTS)
all: $(PROJECTS)
$(PROJECTS):
	$(MAKE) -C $@ all
test:
	@set -e; for project in $(PROJECTS); do $(MAKE) -C $$project test; done
clean:
	@set -e; for project in $(PROJECTS); do $(MAKE) -C $$project clean; done
