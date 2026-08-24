all: diskmanager foosh

diskmanager: diskmanager.c
	gcc diskmanager.c -o diskmanager

foosh: foosh.c diskutils.c
	gcc foosh.c diskutils.c -o foosh

clean:
	rm -f diskmanager foosh