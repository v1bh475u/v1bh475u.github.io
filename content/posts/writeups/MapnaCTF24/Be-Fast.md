+++
date = '2024-02-06T19:23:33+05:30'
title = 'Be Fast'
author = 'vibhatsu'
tags = ["cryptography","writeup"]
cover = ""
coverCaption = ""
description = "A short writeup for Be Fast challenge from Mapna CTF 2024"
readingTime = true
keywords = ["MapnaCTF","Cryptography","DES"]
+++
# Be Fast
We have been given the following python script:
```python
#!/usr/bin/env python3

from random import *
from binascii import *
from Crypto.Cipher import DES
from signal import *
import sys, os
# from flag import flag

def die(*args):
	pr(*args)
	quit()

def pr(*args):
	s = " ".join(map(str, args))
	sys.stdout.write(s + "\n")
	sys.stdout.flush()

def sc():
	return sys.stdin.buffer.readline()

def shift(msg, l):
	assert l < len(msg)
	return msg[l:] + msg[:l]

def pad(text):
	if len(text) % 8 != 0:
		text += (b'\xff' * (8 - len(text) % 8))
	return text

def encrypt(msg, key):
	msg = pad(msg)
	assert len(msg) % 8 == 0
	assert len(key) == 8
	des = DES.new(key, DES.MODE_ECB)
	enc = des.encrypt(msg)
	return enc

def main():
	border = "+"
	pr(border*72)
	pr(border, ".::        Hi all, you should be fast, I mean super fact!!       ::.", border)
	pr(border, "You should send twenty 8-byte keys to encrypt the secret message and", border)
	pr(border, "just decrypt the ciphertext to get the flag, Are you ready to start?", border)
	pr(border*72)

	secret_msg = b'TOP_SECRET:' + os.urandom(40)
	
	cnt, STEP, KEYS = 0, 14, []
	md = 1

	while True:
		pr(border, "please send your key as hex: ")
		alarm(md + 1)
		ans = sc().decode().strip()
		alarm(0)
		try:
			key = unhexlify(ans)
			if len(key) == 8 and key not in KEYS:
				KEYS += [key]
				cnt += 1
			else:
				die(border, 'Kidding me!? Bye!!')
		except:
			die(border, 'Your key is not valid! Bye!!')
		if len(KEYS) == STEP:
			print(KEYS)
			HKEY = KEYS[:7]
			shuffle(HKEY)
			NKEY = KEYS[-7:]
			shuffle(NKEY)
			for h in HKEY: NKEY = [key, shift(key, 1)] + NKEY
			enc = encrypt(secret_msg, NKEY[0])
			for key in NKEY[1:]:
				enc = encrypt(enc, key)
			pr(border, f'enc = {hexlify(enc)}')
			pr(border, f'Can you guess the secret message? ')
			alarm(md + 1)
			msg = sc().strip()
			alarm(0)
			if msg == hexlify(secret_msg):
				die(border, f'Congrats, you deserve the flag: {flag}')
			else:
				die(border, f'Sorry, your input is incorrect! Bye!!')

if __name__ == '__main__':
	main()
```

Here, as we can see there is DES encryption being used. We need to quickly reply to the sever to get the flag.

Firstly, notice that we need to give 14 keys, out of which 2 groups are getting shuffled. Then only last 7 keys are being used. The last key that we give is getting shifted by 1 and being added to `NKEY`  along with the original. Then as result, it is getting encrypted 21 times.

Now, as the keys are randomly shuffled, we cannot brute-force the combination. We also know that the message that we need to decrypt starts with `TOP_SECRET`. 

Now, here comes the vulnerability in DES encryption. It discards every 8th bit. Hence, if we send keys with varying only in every 8th bit, then the order of keys won’t matter. 

```python
from pwn import *
from random import *
from binascii import *
from Crypto.Cipher import DES
from Crypto.Util.number import *
from signal import *
h="3.75.180.117"
p=37773
r=remote(h,p)
context.log_level='DEBUG'
def pad(text):
	if len(text) % 8 != 0:
		text += (b'\xff' * (8 - len(text) % 8))
	return text
def shift(msg, l):
	assert l < len(msg)
	return msg[l:] + msg[:l]
def encrypt(msg, key):
	msg = pad(msg)
	assert len(msg) % 8 == 0
	assert len(key) == 8
	des = DES.new(key, DES.MODE_ECB)
	enc = des.encrypt(msg)
	return enc
def decrypt(msg, key):
	# msg = pad(msg)
	# assert len(msg) % 8 == 0
	# assert len(key) == 8
	des = DES.new(key, DES.MODE_ECB)
	dec = des.decrypt(msg)
	return dec
KEYS=[b'bdd0000b',b'bde0000b',b'bed0000b',b'bee0000b',b'cdd0000b',b'cee0000b',b'ced0000b',b'cde0000b',b'cee0000c',b'ced0000c',b'cde0000c',b'cdd0000c',b'bee0000c',b'bed0000c']
HKEYS=[hexlify(k) for k in KEYS]
r.recvline()
r.recvline()
r.sendlineafter(b'\n',HKEYS[0])
r.recvline()
for i in HKEYS[1:]:
	r.sendline(i)
	r.recvline()

r.recvuntil(']')
r.recvline()
enc = r.recvline()
enc = enc.decode()[10:-2]
print(enc)

NKEY=KEYS[-7:]
last=KEYS[13]
lasshft=shift(KEYS[13],1)
#msg=b'TOP_SECRET:'+os.urandom(40)
#for h in range(7): NKEY = [last, shift(last, 1)] + NKEY
#enc = encrypt(msg, NKEY[0])
#for key in NKEY[1:]:
#	enc = encrypt(enc, key)
#print(enc)
enc=enc.encode()
for i in range(7):
	enc=decrypt(enc,last)
for i in range(7):
	enc=decrypt(enc,lasshft)
	enc=decrypt(enc,last)
print(enc)

print(hexlify(enc))
r.sendline(hexlify(enc))
r.recvline()
r.recvline()
```

Running this script gives us the flag.

```
MAPNA{DES_h4s_A_f3W_5pec1f!c_kEys_7eRm3d_we4K_k3Ys_And_Sem1-wE4k_KeY5!}
```