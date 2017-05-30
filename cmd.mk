# $Id$

#all:: ${SRC:.java=.class}

clean:
	${RM} *.class *.o *~ *.bak

co:
	-${CO} RCS/*

ci:
	-${CI} -u RCS/*

lock:
	-${CO} -l RCS/*

wc:
	${WC} *.java
