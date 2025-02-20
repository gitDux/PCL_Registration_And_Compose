
#include "stdafx.h"
#include <iostream>
#include <fstream>
#include<string>

#include "config.h"
#include "Calibrate.h"
#include "PairAlign.h"

float config_num[50];

void ArgvConfig()
{
	Get_ConfigNum();
	board_size.width = (int)(config_num[0]); // 6;
	board_size.height = (int)(config_num[1]); // 9;
	square_size.width = config_num[2]; // 15;
	square_size.height = config_num[3]; // 15;
	Registration_flag = (int)(config_num[4]);
	Camera_ID == (int)(config_num[5]); // 1;
	KSearchnum = (int)(config_num[6]); // 
	MaxCorrespondenceDistance = config_num[7]; //对应点之间的最大距离（0.1）, 在配准过程中，忽略大于该阈值的点
	LeafSize = config_num[8]; 
	TransformationEpsilon = config_num[9];//允许最大误差
	downsample_flag = (bool)(config_num[10]);
	Cali_Pic_Num = (int)(config_num[11]);
	GetRough_T_flag = (int)(config_num[12]);
	for (int i = 0; i < 12; i++)
	{
		GetR360Rough_T(i/4, i%4) = config_num[i+13];
	}
}

void Get_ConfigNum()
{
	std::string a[50];              //采用 string 类型，存100行的文本，不要用数组 

	int i = 0;
	std::ifstream infile;

	//try
	//{
	infile.open("../config/config.txt", std::ios::in);
	//}
	

	while (!infile.eof())            // 若未到文件结束一直循环 
	{
		getline(infile, a[i], '\n');//读取一行，以换行符结束，存入 a[] 中
		i++;                    //下一行
	}
	for (int ii = 0; ii<i; ii++)        // 显示读取的txt内容 
	{
		std::cout << a[ii] << std::endl;
		std::string::size_type position = a[ii].find(":");
		a[ii]=a[ii].substr(position+1);
		config_num[ii] = std::atof(a[ii].c_str());
	}
	infile.close();
}