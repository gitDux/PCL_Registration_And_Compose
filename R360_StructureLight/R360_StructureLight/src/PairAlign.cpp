//http://blog.csdn.net/xuezhisdc/article/details/51030943

#include "stdafx.h"
#include "PairAlign.h"
#include "Calibrate.h"

#include <boost/make_shared.hpp> //共享指针
//点/点云
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/point_representation.h>
//pcd文件输入/输出
#include <pcl/io/pcd_io.h>
//ply文件输入/输出
#include <pcl/io/ply_io.h>  
#include <pcl/io/ply/ply.h>
//滤波
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/filter.h>
//特征
#include <pcl/features/normal_3d.h>
//配准
#include <pcl/registration/icp.h> //ICP方法
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/transforms.h>
//可视化
#include <pcl/visualization/pcl_visualizer.h>
#include "cv.h"

//命名空间
using pcl::visualization::PointCloudColorHandlerGenericField;
using pcl::visualization::PointCloudColorHandlerCustom;


//全局变量
//可视化对象
pcl::visualization::PCLVisualizer *p;
//左视区和右视区，可视化窗口分成左右两部分
int vp_1, vp_2;

int total_clude;//总点云数
int Registration_flag=2;//0:转台 1：icp 2：转台+icp
int KSearchnum = 30;
float MaxCorrespondenceDistance = 0.1; //对应点之间的最大距离（0.1）, 在配准过程中，忽略大于该阈值的点
float LeafSize = 0.05;
float TransformationEpsilon = 1e-6;//允许最大误差
bool downsample_flag = true;
int GetRough_T_flag = 1;//1表示重新标定获得，0表示已保存直接读取
Eigen::Matrix4f GetR360Rough_T = Eigen::Matrix4f::Identity();


// 定义新的点表达方式< x, y, z, curvature > 坐标+曲率
class MyPointRepresentation : public pcl::PointRepresentation <PointNormalT> //继承关系
{
	using pcl::PointRepresentation<PointNormalT>::nr_dimensions_;
public:
	MyPointRepresentation()
	{
		//指定维数
		nr_dimensions_ = 4;
	}

	//重载函数copyToFloatArray，以定义自己的特征向量
	virtual void copyToFloatArray(const PointNormalT &p, float * out) const
	{
		//< x, y, z, curvature > 坐标xyz和曲率
		out[0] = p.x;
		out[1] = p.y;
		out[2] = p.z;
		out[3] = p.curvature;
	}
};

void CvMatToMatrix4fzk(Eigen::Matrix4f *pcl_T, CvMat *cv_T)
{
	for (int i = 0; i < 4;i++)
	{
		for (int j = 0; j < 4; j++)
		{
			(*pcl_T)(i, j) = CV_MAT_ELEM(*cv_T, float, i, j);
		}
	}
}

//在窗口的左视区，简单的显示源点云和目标点云
void showCloudsLeft(const PointCloud::Ptr cloud_target, const PointCloud::Ptr cloud_source)
{
	p->removePointCloud("vp1_target"); //根据给定的ID，从屏幕中去除一个点云。参数是ID
	p->removePointCloud("vp1_source"); //
	PointCloudColorHandlerCustom<PointT> tgt_h(cloud_target, 0, 255, 0); //目标点云绿色
	PointCloudColorHandlerCustom<PointT> src_h(cloud_source, 255, 0, 0); //源点云红色
	p->addPointCloud(cloud_target, tgt_h, "vp1_target", vp_1); //加载点云
	p->addPointCloud(cloud_source, src_h, "vp1_source", vp_1);
	PCL_INFO("Press Q To Begin The Registration.\n"); //在命令行中显示提示信息
	p->spin();
}


//在窗口的右视区，简单的显示源点云和目标点云
void showCloudsRight(const PointCloudWithNormals::Ptr cloud_target, const PointCloudWithNormals::Ptr cloud_source)
{
	p->removePointCloud("source"); //根据给定的ID，从屏幕中去除一个点云。参数是ID
	p->removePointCloud("target");
	PointCloudColorHandlerGenericField<PointNormalT> tgt_color_handler(cloud_target, "curvature"); //目标点云彩色句柄
	if (!tgt_color_handler.isCapable())
		PCL_WARN("Cannot Create Curvature Color Handler!");
	PointCloudColorHandlerGenericField<PointNormalT> src_color_handler(cloud_source, "curvature"); //源点云彩色句柄
	if (!src_color_handler.isCapable())
		PCL_WARN("Cannot Create Curvature Color Handler!");
	p->addPointCloud(cloud_target, tgt_color_handler, "target", vp_2); //加载点云
	p->addPointCloud(cloud_source, src_color_handler, "source", vp_2);
	p->spinOnce();
}


// 读取一系列的PCD文件（希望配准的点云文件）
// 参数argc 参数的数量（来自main()）
// 参数argv 参数的列表（来自main()）
// 参数models 点云数据集的结果向量
void loadData(int argc, char **argv, std::vector<PCD, Eigen::aligned_allocator<PCD> > &models)
{

	SetCurrentDirectory("..\\input\\");  ////设置路径——杜晓峰

	std::string extension(".ply"); //声明并初始化string类型变量extension，表示文件后缀名
	// 通过遍历文件名，读取pcd文件
	for (int i = 1; i < argc; i++) //遍历所有的文件名（略过程序名）
	{
		std::string fname = std::string(argv[i]);
		if (fname.size() <= extension.size()) //文件名的长度是否符合要求
			continue;

		std::transform(fname.begin(), fname.end(), fname.begin(), (int(*)(int))tolower); //将某操作(小写字母化)应用于指定范围的每个元素
		//检查文件是否是xxx文件
		if (fname.compare(fname.size() - extension.size(), extension.size(), extension) == 0)
		{
			// 读取点云，并保存到models
			PCD m;
			m.f_name = argv[i];
			if (extension == ".ply")
			{
				pcl::PLYReader reader;
				if (reader.read(argv[i], *m.cloud) < 0)
					std::cout << "打开失败" << endl;
			}
			else if (extension == ".pcd")
			{
				pcl::io::loadPCDFile(argv[i], *m.cloud); //读取点云数据
			}
			//去除点云中的NaN点（xyz都是NaN）
			std::vector<int> indices; //保存去除的点的索引
			pcl::removeNaNFromPointCloud(*m.cloud, *m.cloud, indices); //去除点云中的NaN点

			models.push_back(m);
		}
	}
	//检查用户数据
	if (models.empty())
	{
		PCL_ERROR("Syntax is: %s <source.pcd> <target.pcd> [*]", argv[0]); //语法
		PCL_ERROR("[*] - multiple files can be added. The registration results of (i, i+1) will be registered against (i+2), etc"); //可以使用多个文件
	}
	PCL_INFO("Loaded %d datasets.\n", (int)models.size()); //显示读取了多少个点云文件

	SetCurrentDirectory("..\\R360_StructureLight\\");  //设置路径——杜晓峰
}

void roughTranslation(PointCloud::Ptr cloud, Eigen::Matrix4f &T,int n=1)//粗略的将一片点云变换到另一个点云的位置，n便是连续变换次数
{
	Eigen::Matrix4f T_temp=Eigen::Matrix4f::Identity();
	PointCloud::Ptr temp(new PointCloud); //创建临时点云指针
	*temp = *cloud;
	for (int i = 0; i < n; i++)
	{
		T_temp = T_temp*T;//一共转了几次就乘几下
	}
	pcl::transformPointCloud(*temp, *cloud, T_temp);
}

//简单地配准一对点云数据，并返回结果
//参数cloud_src  源点云
//参数cloud_tgt  目标点云
//参数output     输出点云
//参数final_transform 成对变换矩阵 a pair of 
//参数downsample 是否下采样
void pairAlign(const PointCloud::Ptr cloud_src, const PointCloud::Ptr cloud_tgt, PointCloud::Ptr output, Eigen::Matrix4f &final_transform, bool downsample)//把cloud_tgt移动到cloud_src
{
	//
	//为了一致性和速度，下采样
	// \note enable this for large datasets
	PointCloud::Ptr src(new PointCloud); //创建点云指针
	PointCloud::Ptr tgt(new PointCloud);
	pcl::VoxelGrid<PointT> grid; //VoxelGrid 把一个给定的点云，聚集在一个局部的3D网格上,并下采样和滤波点云数据
	if (downsample) //下采样
	{
		grid.setLeafSize(LeafSize, LeafSize, LeafSize); //设置体元网格的叶子大小
		//下采样 源点云
		grid.setInputCloud(cloud_src); //设置输入点云
		grid.filter(*src); //下采样和滤波，并存储在src中
		//下采样 目标点云
		grid.setInputCloud(cloud_tgt);
		grid.filter(*tgt);
	}
	else //不下采样
	{
	//	PCL_INFO("不下采样/n");
		src = cloud_src; //直接复制
		tgt = cloud_tgt;
	}

	//计算曲面的法向量和曲率
	PCL_INFO("计算曲面的法向量和曲率/n");
	PointCloudWithNormals::Ptr points_with_normals_src(new PointCloudWithNormals); //创建源点云指针（注意点的类型包含坐标和法向量）
	PointCloudWithNormals::Ptr points_with_normals_tgt(new PointCloudWithNormals); //创建目标点云指针（注意点的类型包含坐标和法向量）
	pcl::NormalEstimation<PointT, PointNormalT> norm_est; //该对象用于计算法向量
	//PCL_INFO("bug here/n");
	pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>()); //创建kd树，用于计算法向量的搜索方法
	//PCL_INFO("bug not here/n");

	norm_est.setSearchMethod(tree); //设置搜索方法
	norm_est.setKSearch(KSearchnum); //设置最近邻的数量

	norm_est.setInputCloud(src); //设置输入云
	norm_est.compute(*points_with_normals_src); //计算法向量，并存储在points_with_normals_src
	pcl::copyPointCloud(*src, *points_with_normals_src); //复制点云（坐标）到points_with_normals_src（包含坐标和法向量）

	norm_est.setInputCloud(tgt); //这3行计算目标点云的法向量，同上
	norm_est.compute(*points_with_normals_tgt);//这句话有点问题
	pcl::copyPointCloud(*tgt, *points_with_normals_tgt);
	//创建一个 自定义点表达方式的 实例
	MyPointRepresentation point_representation;
	//加权曲率维度，以和坐标xyz保持平衡
	float alpha[4] = { 1.0, 1.0, 1.0, 1.0 };
	point_representation.setRescaleValues(alpha); //设置缩放值（向量化点时使用）

	//创建非线性ICP对象 并设置参数
	pcl::IterativeClosestPointNonLinear<PointNormalT, PointNormalT> reg; //创建非线性ICP对象（ICP变体，使用Levenberg-Marquardt最优化）
	reg.setTransformationEpsilon(TransformationEpsilon); //设置容许的最大误差（迭代最优化）
	//***** 注意：根据自己数据库的大小调节该参数
	reg.setMaxCorrespondenceDistance(MaxCorrespondenceDistance);  //设置对应点之间的最大距离（0.1）,在配准过程中，忽略大于该阈值的点
	reg.setPointRepresentation(boost::make_shared<const MyPointRepresentation>(point_representation)); //设置点表达
	//设置源点云和目标点云
	//reg.setInputSource (points_with_normals_src); //版本不符合，使用下面的语句
	reg.setInputCloud(points_with_normals_src); //设置输入点云（待变换的点云）
	reg.setInputTarget(points_with_normals_tgt); //设置目标点云
	reg.setMaximumIterations(2); //设置内部优化的迭代次数

	// Run the same optimization in a loop and visualize the results
	Eigen::Matrix4f Ti = Eigen::Matrix4f::Identity(), prev, targetToSource;
	PointCloudWithNormals::Ptr reg_result = points_with_normals_src; //用于存储结果（坐标+法向量）

	for (int i = 0; i < 30; ++i) //迭代
	{
		PCL_INFO("Iteration Nr. %d.\n", i); //命令行显示迭代的次数
		//保存点云，用于可视化
		points_with_normals_src = reg_result; //
		//估计
		//reg.setInputSource (points_with_normals_src);
		reg.setInputCloud(points_with_normals_src); //重新设置输入点云（待变换的点云），因为经过上一次迭代，已经发生变换了
		reg.align(*reg_result); //对齐（配准）两个点云

		Ti = reg.getFinalTransformation() * Ti; //累积（每次迭代的）变换矩阵
		//如果这次变换和上次变换的误差比阈值小，通过减小最大的对应点距离的方法来进一步细化
		if (fabs((reg.getLastIncrementalTransformation() - prev).sum()) < reg.getTransformationEpsilon())
			reg.setMaxCorrespondenceDistance(reg.getMaxCorrespondenceDistance() - 0.001); //减小对应点之间的最大距离（上面设置过）
		prev = reg.getLastIncrementalTransformation(); //上一次变换的误差
		//显示当前配准状态，在窗口的右视区，简单的显示源点云和目标点云
		showCloudsRight(points_with_normals_tgt, points_with_normals_src);
	}

	targetToSource = Ti.inverse(); //计算从目标点云到源点云的变换矩阵
	pcl::transformPointCloud(*cloud_tgt, *output, targetToSource); //将目标点云 变换回到 源点云帧。这一句是精华

	p->removePointCloud("source"); //根据给定的ID，从屏幕中去除一个点云。参数是ID
	p->removePointCloud("target");
	PointCloudColorHandlerCustom<PointT> cloud_tgt_h(output, 0, 255, 0); //设置点云显示颜色，下同
	PointCloudColorHandlerCustom<PointT> cloud_src_h(cloud_src, 255, 0, 0);
	p->addPointCloud(output, cloud_tgt_h, "target", vp_2); //添加点云数据，下同
	p->addPointCloud(cloud_src, cloud_src_h, "source", vp_2);

	PCL_INFO("Press Q To Continue The Registration.\n");
	p->spin();

	p->removePointCloud("source");
	p->removePointCloud("target");

	//add the source to the transformed target
	*output += *cloud_src; // 拼接点云图（的点）点数数目是两个点云的点数和

	final_transform = targetToSource; //最终的变换。目标点云到源点云的变换矩阵
}

void AccurateRegistration(std::vector<PCD, Eigen::aligned_allocator<PCD> > &data_temp)//先把所有点云移动到1，然后再已1为基础拼接其他的
{
	//创建一个 PCLVisualizer 对象
	p = new pcl::visualization::PCLVisualizer("Pairwise Incremental Registration example"); //p是全局变量
	p->createViewPort(0.0, 0, 0.5, 1.0, vp_1); //创建左视区
	p->createViewPort(0.5, 0, 1.0, 1.0, vp_2); //创建右视区

	//创建点云指针和变换矩阵
	PointCloud::Ptr result(new PointCloud), source(new PointCloud), target; //创建3个点云指针，分别用于结果，源点云和目标点云
	//全局变换矩阵，单位矩阵，成对变换
	//逗号表达式，先创建一个单位矩阵，然后将成对变换 赋给 全局变换
	Eigen::Matrix4f pairTransform;//GlobalTransform = Eigen::Matrix4f::Identity(), 
	Eigen::Matrix4f T1 = Eigen::Matrix4f::Identity(), T2 = Eigen::Matrix4f::Identity(), R360Plant_Transform = Eigen::Matrix4f::Zero(), T_Pro2Cam = Eigen::Matrix4f::Identity(), T_Pro2chessboard = Eigen::Matrix4f::Identity(), T_Cam2chessboard = Eigen::Matrix4f::Identity(), T_Rz180 = Eigen::Matrix4f::Identity();
	
	if (GetRough_T_flag == 1)
	{
		CvMatToMatrix4fzk(&T_Pro2chessboard, Cam_extrinsic_matrix);
		CvMatToMatrix4fzk(&T_Cam2chessboard, Pro_extrinsic_matrix);
		T_Pro2Cam = T_Pro2chessboard*(T_Cam2chessboard.inverse());
//		T_Pro2Cam = T_Pro2chessboard;
		T_Rz180(0, 0) = -1;
		T_Rz180(1, 1) = -1;

		//计算变换矩阵：p1=T1p0,p2=T2p0,p1=T1*inverserT2p2   (p0和p1,p2是对应点，所以当p1与p2重合的变换就是所求),将点云2移动到1,T1,T2是指从点云坐标系（根据TI代码，是投影仪坐标系（不排除是摄像机坐标系的可能）绕Z旋转180°得到）变换到棋盘坐标系
		for (int i = 0; i < (T_mat_4x4.size() - 1); i++)//计算两两标定得到的矩阵，相加然后求平均（也算某种意义的平均齐次变换吧）
		{
			CvMatToMatrix4fzk(&T1, &(T_mat_4x4[i]));
			CvMatToMatrix4fzk(&T2, &(T_mat_4x4[i + 1]));

//			R360Plant_Transform += ((T_Rz180*T1)) * ((T_Rz180*T2).inverse());//此处假设是相机坐标系，若是投影仪坐标系则((T_Rz180*T_Pro2Cam*T1)) * ((T_Rz180*T_Pro2Cam*T2).inverse())
			R360Plant_Transform += ((T_Rz180*T_Pro2Cam*T1)) * (((T_Rz180*T_Pro2Cam*T2)).inverse());//将1转到2此处假设点云是基于投影仪坐标系
			std::cout << T1 << endl;
			std::cout << T2 << endl;
			std::cout << "T1*(T2.inverse()):" << endl;
			std::cout << T1*(T2.inverse()) << endl;
			std::cout << ((T_Rz180*T_Pro2Cam*T1)) * (((T_Rz180*T_Pro2Cam*T2)).inverse()) << endl;
			std::cout << R360Plant_Transform << endl;
		}
		R360Plant_Transform = R360Plant_Transform / (T_mat_4x4.size() - 1);
		std::cout << R360Plant_Transform << endl;//测试算的对不对
	}
	else if (GetRough_T_flag == 0)
	{
		//CvMat *temp1 = cvCreateMat(4, 4, CV_64FC1);
		//CvMat *temp2 = cvCreateMat(4, 4, CV_32FC1);

		//CvFileStorage *fs1;
		//fs1 = cvOpenFileStorage(".//result//Rough_T.xml", 0, CV_STORAGE_READ);
		//if (fs1)
		//{
		//	*temp1 = *cvCloneMat((CvMat *)cvReadByName(fs1, NULL, "Rough_T"));
		//	cvReleaseFileStorage(&fs1);
		//}
		//else
		//{
		//	cout << "Error: can not find the Rough_T!!!!!" << endl;
		//}

		//for (int i = 0; i < 4; i++)
		//{
		//	for (int j = 0; j < 4; j++)
		//	{
		//		CV_MAT_ELEM(*temp2, float, i, j) = CV_MAT_ELEM(*temp1, double, i, j);
		//	}
		//}
		//CvMatToMatrix4fzk(&R360Plant_Transform, temp2);
		R360Plant_Transform = GetR360Rough_T;
		std::cout << R360Plant_Transform << endl;
	}

	if ((Registration_flag == 0) || (Registration_flag == 2))
	{
		//粗拼接（转台齐次变换）
		for (size_t i = 1; i < data_temp.size(); ++i)
		{
			roughTranslation(data_temp[i].cloud, R360Plant_Transform, i);//将所有点云移动到1号点云的位置
		}
	}
	//精拼接，遍历所有的点云文件
//	PointCloud::Ptr temp(new PointCloud); //创建临时点云指针
	*result = *(data_temp[0].cloud);
	for (size_t i = 1; i < data_temp.size(); ++i)//两两配准，1+2，把2移到1位置
	{
		*source = *result; //源点云
		target = data_temp[i].cloud; //目标点云
		showCloudsLeft(source, target); //在左视区，简单的显示源点云和目标点云		

		//显示正在配准的点云文件名和各自的点数
		PCL_INFO("Aligning %s (%d points) with %s (%d points).\n", data_temp[i - 1].f_name.c_str(), source->points.size(), data_temp[i].f_name.c_str(), target->points.size());

		//********************************************************
		if ((Registration_flag == 1) || (Registration_flag == 2))
		{
			//配准2个点云，函数定义见上面
			pairAlign(target, source, result, pairTransform, downsample_flag);//temp就是将source拼target在合并的点云
		}
		else if (Registration_flag == 0)
		{
			*result = *source + *target;
		}
		//********************************************************

		p->removePointCloud("source"); //根据给定的ID，从屏幕中去除一个点云。参数是ID
		p->removePointCloud("target");
		PointCloudColorHandlerCustom<PointT> cloud_tgt_a(result, 0, 255, 0); //设置点云显示颜色，下同
		//		PointCloudColorHandlerCustom<PointT> cloud_src_h(cloud_src, 255, 0, 0);
		p->addPointCloud(result, cloud_tgt_a, "target", vp_2); //添加点云数据，下同
		//		p->addPointCloud(cloud_src, cloud_src_h, "source", vp_2);

		PCL_INFO("Processing...\n");
		p->spinOnce();
		//		Sleep(5000);
		//p->removePointCloud("source");
		//p->removePointCloud("target");
	}
	char s[30];
	std::cout << "Please Enter Output File Name：" << endl;
	std:cin >> s;
	std::stringstream output_filename; //这两句是生成文件名
	output_filename << ".//result//" << s << ".ply";
	pcl::PLYWriter writer;
	writer.write(output_filename.str(), *result);
//	pcl::io::savePCDFile(ss.str(), *result); //保存成对的配准结果
}

void AccurateRegistration2(std::vector<PCD, Eigen::aligned_allocator<PCD> > &data_temp)//将1移动到2再icp拼接，在将结果移动到3与3icp
{
	//创建一个 PCLVisualizer 对象
	p = new pcl::visualization::PCLVisualizer("Pairwise Incremental Registration example"); //p是全局变量
	p->createViewPort(0.0, 0, 0.5, 1.0, vp_1); //创建左视区
	p->createViewPort(0.5, 0, 1.0, 1.0, vp_2); //创建右视区

	//创建点云指针和变换矩阵
	PointCloud::Ptr result(new PointCloud), source(new PointCloud), target; //创建3个点云指针，分别用于结果，源点云和目标点云
	//全局变换矩阵，单位矩阵，成对变换
	//逗号表达式，先创建一个单位矩阵，然后将成对变换 赋给 全局变换
	Eigen::Matrix4f pairTransform;//GlobalTransform = Eigen::Matrix4f::Identity(), 
	Eigen::Matrix4f T1 = Eigen::Matrix4f::Identity(), T2 = Eigen::Matrix4f::Identity(), R360Plant_Transform = Eigen::Matrix4f::Zero(), T_Pro2Cam = Eigen::Matrix4f::Identity(), T_Pro2chessboard = Eigen::Matrix4f::Identity(), T_Cam2chessboard = Eigen::Matrix4f::Identity(), T_Rz180 = Eigen::Matrix4f::Identity();

	if (GetRough_T_flag == 1)
	{
		CvMatToMatrix4fzk(&T_Pro2chessboard, Cam_extrinsic_matrix);
		CvMatToMatrix4fzk(&T_Cam2chessboard, Pro_extrinsic_matrix);
		T_Pro2Cam = T_Pro2chessboard*(T_Cam2chessboard.inverse());
		T_Rz180(0, 0) = -1;
		T_Rz180(1, 1) = -1;

		//计算变换矩阵：p1=T1p0,p2=T2p0,p1=T1*inverserT2p2   (p0和p1,p2是对应点，所以当p1与p2重合的变换就是所求),将点云2移动到1,T1,T2是指从点云坐标系（根据TI代码，是投影仪坐标系（不排除是摄像机坐标系的可能）绕Z旋转180°得到）变换到棋盘坐标系
		for (int i = 0; i < (T_mat_4x4.size() - 1); i++)//计算两两标定得到的矩阵，相加然后求平均（也算某种意义的平均齐次变换吧）
		{
			CvMatToMatrix4fzk(&T1, &(T_mat_4x4[i]));
			CvMatToMatrix4fzk(&T2, &(T_mat_4x4[i + 1]));

			R360Plant_Transform += ((T_Rz180*T2)) * ((T_Rz180*T1).inverse());
			std::cout << T1 << endl;
			std::cout << T2 << endl;
			std::cout << ((T_Rz180*T2)) * ((T_Rz180*T1).inverse()) << endl;//此处假设是相机坐标系
			std::cout << R360Plant_Transform << endl;
		}
		R360Plant_Transform = R360Plant_Transform / (T_mat_4x4.size() - 1);
		std::cout << R360Plant_Transform << endl;//测试算的对不对
	}
	else if (GetRough_T_flag == 0)
	{
		R360Plant_Transform = GetR360Rough_T.inverse();
		std::cout << GetR360Rough_T << endl;
		std::cout << R360Plant_Transform << endl;
	}

	//精拼接，遍历所有的点云文件
	//	PointCloud::Ptr temp(new PointCloud); //创建临时点云指针
	*result = *(data_temp[0].cloud);
	for (size_t i = 1; i < data_temp.size(); ++i)//两两配准，1+2，把2移到1位置
	{
		*source = *result; //源点云
		target = data_temp[i].cloud; //目标点云

		if ((Registration_flag == 0) || (Registration_flag == 2))
		{
			roughTranslation(source, R360Plant_Transform, 1);//将面结果点云移动到下一点云的位置	
		}
		showCloudsLeft(source, target); //在左视区，简单的显示源点云和目标点云		

		//显示正在配准的点云文件名和各自的点数
		PCL_INFO("Aligning %s (%d points) with %s (%d points).\n", data_temp[i - 1].f_name.c_str(), source->points.size(), data_temp[i].f_name.c_str(), target->points.size());

		//********************************************************

		if ((Registration_flag == 1) || (Registration_flag == 2))
		{
			//配准2个点云，函数定义见上面

			pairAlign(target, source, result, pairTransform, downsample_flag);//temp就是将src拼在target合并的点云

		}
		else if (Registration_flag == 0)
		{
			*result = *source + *target;
		}
		//********************************************************

		p->removePointCloud("source"); //根据给定的ID，从屏幕中去除一个点云。参数是ID
		p->removePointCloud("target");
		PointCloudColorHandlerCustom<PointT> cloud_tgt_a(result, 0, 255, 0); //设置点云显示颜色，下同
		//		PointCloudColorHandlerCustom<PointT> cloud_src_h(cloud_src, 255, 0, 0);
		p->addPointCloud(result, cloud_tgt_a, "target", vp_2); //添加点云数据，下同
		//		p->addPointCloud(cloud_src, cloud_src_h, "source", vp_2);

		PCL_INFO("Processing...\n");
		p->spinOnce();
		//		Sleep(5000);
		//p->removePointCloud("source");
		//p->removePointCloud("target");
	}
	char s[30];
	std::cout << "Please Enter Output File Name：" << endl;
    std:cin >> s;
	std::stringstream output_filename; //这两句是生成文件名
	output_filename << "..//output//" << s << ".ply";
	pcl::PLYWriter writer;
	writer.write(output_filename.str(), *result);
	//	pcl::io::savePCDFile(ss.str(), *result); //保存成对的配准结果
}


