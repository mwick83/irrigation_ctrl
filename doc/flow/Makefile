DIAS=$(wildcard *.dia)
PNGS=$(patsubst %.dia, %.png, $(DIAS))

all: convert
convert: $(PNGS)

$(PNGS): %.png : %.dia
	dia -e $@ -n -s 500x -t png $<
