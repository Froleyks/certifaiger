SHELL := /bin/bash

all:
	@failed="$$(comm -3 <(parallel '../bin/check {= s/witness/model/ =} {} >/dev/null 2>&1 || echo {}' ::: *_witness.* | sort) <(sort expected-invalid))"; \
	[ -n "$$failed" ] && printf 'FAILED:\n%s\n' "$$failed" && exit 1 || true
