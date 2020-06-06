#ifndef IMAGE_H
#define IMAGE_H
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "libjpeg/jpeglib.h"
#include <iostream>
static void InitBuffer(jpeg_compress_struct* cinfo) { }
static boolean EmptyBuffer(jpeg_compress_struct* cinfo) { return TRUE; }
static void TermBuffer(jpeg_compress_struct* cinfo) { }

void jpeg_mem_error_exit (j_common_ptr cinfo) 
{
	char err_msg[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message) (cinfo,err_msg);
	std::cout<<"jpeg_mem_error_exit:"<<err_msg<<std::endl;
}

void convertToJpeg(unsigned char *pDstBuf, int* buf_size, int width, int height, int num_channels, const unsigned char *pImage_data)
{
    int bufferSize=*buf_size;
	struct jpeg_destination_mgr dmgr;
	dmgr.init_destination    = InitBuffer;
	dmgr.empty_output_buffer = EmptyBuffer;
	dmgr.term_destination    = TermBuffer;
	dmgr.next_output_byte    = pDstBuf;
	dmgr.free_in_buffer      = bufferSize;  
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr       jerr;
	cinfo.err = jpeg_std_error(&jerr);
	//设置自定义的错误处理函数
	jerr.error_exit = jpeg_mem_error_exit;
	jpeg_create_compress(&cinfo);

	cinfo.dest = &dmgr;
	cinfo.image_width      = width;
	cinfo.image_height     = height;
	cinfo.input_components = num_channels;     //单通道图像
	cinfo.in_color_space   = JCS_GRAYSCALE;    //灰度图像
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality (&cinfo, 80, TRUE);       //压缩率为80%
	jpeg_start_compress(&cinfo, TRUE);
	int rowSize = cinfo.image_width * cinfo.input_components;
	JSAMPROW rowPointer;
	const unsigned char * pixels = pImage_data;

	// Write the JPEG data
	while (cinfo.next_scanline < cinfo.image_height)
	{
		rowPointer = (JSAMPROW) &pixels[cinfo.next_scanline * rowSize];
		jpeg_write_scanlines(&cinfo, &rowPointer, 1);
	}
	std::cout<<"cinfo.next_scanline:"<<cinfo.next_scanline<<std::endl;
	jpeg_finish_compress(&cinfo);
	*buf_size = bufferSize - dmgr.free_in_buffer;
	jpeg_destroy_compress(&cinfo);
}
#endif
