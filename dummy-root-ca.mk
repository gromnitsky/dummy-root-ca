#!/usr/bin/make -f

subjectAltName :=

all: $(shell hostname -f).crt

root.crt: root.pem
	openssl req -x509 -key $< -out $@ -days 3650 \
	 -subj /CN="$$USER Test Root CA" \
	 -addext basicConstraints=critical,CA:TRUE,pathlen:0 \
	 -addext keyUsage=critical,keyCertSign,cRLSign \
	 -addext nsCertType=sslCA,emailCA,objCA

.PRECIOUS: %.pem
%.pem:; openssl genrsa 4096 > $@

%.crt: %.pem root.crt
	openssl req -x509 -key $< -CA root.crt -CAkey root.pem -out $@ \
	 -days 365 \
	 -subj /CN=$(basename $@) \
	 -addext subjectAltName=$(or $(subjectAltName),$(mk_subjectAltName)) \
	 -addext basicConstraints=critical,CA:FALSE \
	 -addext extendedKeyUsage=serverAuth \
	 -addext keyUsage=digitalSignature,nonRepudiation,keyEncipherment,keyAgreement

mk_subjectAltName = DNS:$(basename $@),$(shell ip -br addr | awk '{split($$3, a, "/"); print "IP:"a[1]}' | paste -s -d,)
