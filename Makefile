PYTHON ?= python3
.PHONY: help check check-python check-c check-c-portable prototype regenerate doctor upstream-check
help:
	@printf '%s\n' 'CTR2WUP developer commands:' '  make check             Python and portable C checks with ASan/UBSan' '  make check-python      Original host fixtures (no Wii U SDK)' '  make check-c-portable  Normal C checks, including Windows' '  make prototype         Native RPX/WUHB (verified Wii U SDK required)' '  make doctor            Inspect local tools and dependencies' '  make upstream-check    Original generated-code ABI validation' 'Read docs/repository-guide.md for source layout and docs/development-tracking.md for issue/PR policy.'
check: check-python check-c
check-python:
	$(PYTHON) -m unittest discover -s tests -v
check-c:
	$(MAKE) -C tests/cpu check sanitize
	$(MAKE) -C tests/guest check sanitize
	$(MAKE) -C tests/services check sanitize
	$(MAKE) -C tests/heap check sanitize
	$(MAKE) -C tests/kernel check sanitize
	$(MAKE) -C tests/hid check sanitize
	$(MAKE) -C tests/gsp check sanitize
	$(MAKE) -C tests/gx check sanitize
	$(MAKE) -C tests/pica check sanitize
	$(MAKE) -C tests/vertex check sanitize
	$(MAKE) -C tests/scenes check sanitize
	$(MAKE) -C tests/relay check sanitize
	$(MAKE) -C tests/multiple check sanitize
	$(MAKE) -C tests/threads check sanitize
	$(MAKE) -C tests/filesystem check sanitize
	$(MAKE) -C tests/assets check sanitize
	$(MAKE) -C tests/textures check sanitize
	$(MAKE) -C tests/apt check sanitize
	$(MAKE) -C tests/report check sanitize
	$(MAKE) -C tests/audio check sanitize
	$(MAKE) -C tests/recomp-progress check sanitize
check-c-portable:
	@set -e; for suite in cpu guest services heap kernel hid gsp gx pica vertex scenes relay multiple threads filesystem assets textures apt report audio recomp-progress; do $(MAKE) -C tests/$$suite check; done
prototype:
	$(MAKE) -C runtime/wiiu -j 3
doctor:
	$(PYTHON) -m host doctor
upstream-check:
	$(PYTHON) tools/validate-upstream.py --output artifacts/upstream-check-$(shell date -u +%Y%m%dT%H%M%S)
regenerate:
	$(PYTHON) tests/fixtures/cpu/make_fixture.py
	$(PYTHON) translator/aot.py tests/fixtures/cpu/arithmetic.arm.bin tests/fixtures/cpu/arithmetic.generated.c --base 0x1000 --symbol ctr_arm_fixture
	$(PYTHON) translator/aot.py tests/fixtures/cpu/exit.thumb.bin tests/fixtures/cpu/exit.generated.c --base 0x1100 --symbol ctr_thumb_fixture --thumb
	$(PYTHON) tools/generate-guest.py
	$(PYTHON) tools/generate-service-fixture.py
	$(PYTHON) tools/generate-heap-fixture.py
	$(PYTHON) tools/generate-kernel-fixture.py
	$(PYTHON) tools/generate-hid-fixture.py
	$(PYTHON) tools/generate-gsp-fixture.py
	$(PYTHON) tools/generate-relay-fixture.py
	$(PYTHON) tools/generate-stack-fixture.py
	$(PYTHON) tools/generate-thread-fixture.py
