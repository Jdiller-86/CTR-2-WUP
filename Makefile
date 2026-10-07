PYTHON ?= python3
.PHONY: check prototype regenerate doctor
check:
	$(PYTHON) -m unittest discover -s tests -v
	$(MAKE) -C tests/cpu check sanitize
	$(MAKE) -C tests/guest check sanitize
prototype:
	$(MAKE) -C runtime/wiiu -j 3
doctor:
	$(PYTHON) -m host doctor
regenerate:
	$(PYTHON) tests/fixtures/cpu/make_fixture.py
	$(PYTHON) translator/aot.py tests/fixtures/cpu/arithmetic.arm.bin tests/fixtures/cpu/arithmetic.generated.c --base 0x1000 --symbol ctr_arm_fixture
	$(PYTHON) translator/aot.py tests/fixtures/cpu/exit.thumb.bin tests/fixtures/cpu/exit.generated.c --base 0x1100 --symbol ctr_thumb_fixture --thumb
	$(PYTHON) tools/generate-guest.py
