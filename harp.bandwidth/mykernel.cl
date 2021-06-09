__kernel void
__attribute__ (( task ) )
mykernel (
	global ulong8 * restrict src ,
	global ulong8 * restrict dst ,
	unsigned int lines ) {

		ulong8 output = ( ulong8 ) (0);
		#pragma ii 1
		for ( unsigned int i = 0; i < lines ; i ++) {
			output += src [ i ];
		}
		*dst = output ;
	}
