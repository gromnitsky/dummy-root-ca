#!/usr/bin/make -f

root.ca := $(USER)\'s Dummy Root CA
tls.altname :=
d := 3650

all: $(shell hostname -f).crt

root.crt: root.pem
	openssl req -x509 -key $< -out $@ -days $(d) \
	 -subj /CN="$(root.ca)" \
	 -addext basicConstraints=critical,CA:TRUE,pathlen:0 \
	 -addext keyUsage=critical,keyCertSign,cRLSign \
	 -addext nsCertType=sslCA,emailCA,objCA

.PRECIOUS: %.pem
%.pem:; openssl genrsa 4096 > $@

%.crt: %.pem root.crt
	openssl req -x509 -key $< -CA root.crt -CAkey root.pem -out $@ \
	 -days $(d) \
	 -subj /CN=$(basename $@) \
	 -addext subjectAltName=$(or $(tls.altname),$(altname)) \
	 -addext basicConstraints=critical,CA:FALSE \
	 -addext extendedKeyUsage=serverAuth \
	 -addext keyUsage=digitalSignature,nonRepudiation,keyEncipherment,keyAgreement

altname = DNS:$(basename $@),$(shell ip -br addr | awk '{split($$3, a, "/"); print "IP:"a[1]}' | paste -s -d,)
