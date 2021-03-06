/*
 * Copyright (c) 2014, ATR, Atsushi Watanabe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the names of its 
 *       contributors may be used to endorse or promote products derived from 
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <boost/bind.hpp>				//boost that C++ library is widely used in the ROS codebase. bind.hpp can bind any argument.
#include <mutex>					//mutual exclusion
#include <ros/ros.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud2_iterator.h>		//like array. repetetion prosessing abstract.
#include <sensor_msgs/MagneticField.h>			//magnetic field
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/point_cloud_conversion.h>		//point cloud header

#include "vssp.hpp"					//format array,etc.


class hokuyo3d_node					//hokuyo3d_node class
{
	public:
		void cbPoint(					//detect points and pulish points
				const vssp::header &header, 			
				const vssp::range_header &range_header, 
				const vssp::range_index &range_index,
				const boost::shared_array<uint16_t> &index,
				const boost::shared_array<vssp::xyzi> &points,
				const std::chrono::microseconds &delayRead)
		{
			ROS_INFO("test cbPoint check");
			if(timestampBase == ros::Time(0)) return;			//not run initially
			// Pack scan data
			if(enablePc)							//enablePc is flag
			{
				if(cloud.points.size() == 0)				//point's size is 0
				{
					// Start packing PointCloud message
					cloud.header.frame_id = frame_id;		//frame id , rviz
					cloud.header.stamp = timestampBase		//timestamp
						+ ros::Duration(range_header.line_head_timestamp_ms * 0.001);		//set timestamp
				}
				// Pack PointCloud message
				for(int i = 0; i < index[range_index.nspots]; i ++)	//range_index.nspots for array
				{
					if(points[i].r < range_min)			//continue
					{
						continue;				//skip below prosess
					}
					geometry_msgs::Point32 point;			
					point.x = points[i].x;
					point.y = points[i].y;
					point.z = points[i].z;
					cloud.points.push_back(point);			//cloud input value
					cloud.channels[0].values.push_back(points[i].i);//cloud input value
				}
			}
			if(enablePc2)					//pcl2 messages. almost like pcl.
			{
				ROS_INFO("test cbPoint enablePc2 check");
				if(cloud2.data.size() == 0)
				{
					// Start packing PointCloud2 message
					cloud2.header.frame_id = frame_id;
					cloud2.header.stamp = timestampBase
						+ ros::Duration(range_header.line_head_timestamp_ms * 0.001);
					cloud2.row_step = 0;
					cloud2.width = 0;
				}
				// Pack PointCloud2 message
				cloud2.data.resize((cloud2.width + index[range_index.nspots])
					   	* cloud2.point_step);

				float *data = reinterpret_cast<float*>(&cloud2.data[0]);
				data += cloud2.width * cloud2.point_step / sizeof(float);
				for(int i = 0; i < index[range_index.nspots]; i ++)
				{
					if(points[i].r < range_min)
					{
						continue;
					}
					*(data++) = points[i].x;
					*(data++) = points[i].y;
					*(data++) = points[i].z;
					*(data++) = points[i].i;
					cloud2.width ++;
				}
				cloud2.row_step = cloud2.width * cloud2.point_step;
			}
			ROS_INFO("test1");
			// Publish points
			if((cycle == CYCLE_FIELD &&
						(range_header.field != field ||
						 range_header.frame != frame)) ||
					(cycle == CYCLE_FRAME &&
						(range_header.frame != frame)) ||
					(cycle == CYCLE_LINE))				//condition flag
			{
				if(enablePc)
				{
					pubPc.publish(cloud);
					cloud.points.clear();
					cloud.channels[0].values.clear();
				}
				if(enablePc2)
				{
					cloud2.data.resize(cloud2.width * cloud2.point_step);
					pubPc2.publish(cloud2);					//pcl2 pub
					cloud2.data.clear();
					ROS_INFO("ctl2 pub succes");
				}
				if(range_header.frame != frame) ping();		//stream string msg
				frame = range_header.frame;
				field = range_header.field;
				line = range_header.line;
			}
		};
		void cbPing(const vssp::header &header, const std::chrono::microseconds &delayRead)			//time function
		{
			ROS_INFO("test cbPing check");
			ros::Time now = ros::Time::now() - ros::Duration(delayRead.count() * 0.001 * 0.001);
			ros::Duration delay = ((now - timePing)
					- ros::Duration(header.send_time_ms * 0.001 - header.received_time_ms * 0.001)) * 0.5;
			ros::Time base = timePing + delay - ros::Duration(header.received_time_ms * 0.001);
			if(timestampBase == ros::Time(0)) timestampBase = base;
			else timestampBase += (base - timestampBase) * 0.01;
		}
		void cbAux(							//maybe imu message
				const vssp::header &header, 
				const vssp::aux_header &aux_header, 
				const boost::shared_array<vssp::aux> &auxs,
				const std::chrono::microseconds &delayRead)
		{
			ROS_INFO("test cbAux check");
			if(timestampBase == ros::Time(0)) return;
			ros::Time stamp = timestampBase + ros::Duration(aux_header.timestamp_ms * 0.001);

			if((aux_header.data_bitfield & (vssp::AX_MASK_ANGVEL | vssp::AX_MASK_LINACC))
					== (vssp::AX_MASK_ANGVEL | vssp::AX_MASK_LINACC))			//flag
			{
				imu.header.frame_id = frame_id;				//imu frame_id
				imu.header.stamp = stamp;				//imu timestmap
				for(int i = 0; i < aux_header.data_count; i ++)
				{
					imu.orientation_covariance[0] = -1.0;		//detect inertial matlix or array
					imu.angular_velocity.x = auxs[i].ang_vel.x;
					imu.angular_velocity.y = auxs[i].ang_vel.y;
					imu.angular_velocity.z = auxs[i].ang_vel.z;
					imu.linear_acceleration.x = auxs[i].lin_acc.x;
					imu.linear_acceleration.y = auxs[i].lin_acc.y;
					imu.linear_acceleration.z = auxs[i].lin_acc.z;
					pubImu.publish(imu);				//pub imu data
					imu.header.stamp += ros::Duration(aux_header.data_ms * 0.001);	//time stamp
				}
			}
			if((aux_header.data_bitfield & vssp::AX_MASK_MAG) == vssp::AX_MASK_MAG )
			{
				mag.header.frame_id = frame_id;				//magnetic frame_id
				mag.header.stamp = stamp;				//timestamp
				for(int i = 0; i < aux_header.data_count; i ++)
				{
					mag.magnetic_field.x = auxs[i].mag.x;		//magnetic array
					mag.magnetic_field.y = auxs[i].mag.y;
					mag.magnetic_field.z = auxs[i].mag.z;
					pubMag.publish(mag);				//pub magnetic
					mag.header.stamp += ros::Duration(aux_header.data_ms * 0.001);	//timestamp
				}
			}
		};
		void cbConnect(bool success)					//driver function for connection. faulth??
		{
			ROS_INFO("test cbConnect check");
			if(success)
			{
				ROS_INFO("Connection established");
				ping();
				driver.setInterlace(interlace);		//set param interlace equals 4. print
				driver.requestHorizontalTable();	//print
				driver.requestVerticalTable();		//print below
				driver.requestData(true, true);		
				driver.requestAuxData();
				driver.receivePackets();
				ROS_INFO("Communication started");	//...
			}
			else
			{
				ROS_ERROR("Connection failed");		//connection faild
			}
		};
		hokuyo3d_node() :						//constructor, declaer functions and variable
			nh("~"),
			timestampBase(0)
		{
			nh.param("interlace", interlace, 4);				//param set and below
			nh.param("ip", ip, std::string("192.168.0.10"));
			nh.param("port", port, 10940);
			nh.param("frame_id", frame_id, std::string("hokuyo3d"));
			nh.param("range_min", range_min, 0.0);				//...

			std::string output_cycle;
			nh.param("output_cycle", output_cycle, std::string("field"));	//param output cycle

			if(output_cycle.compare("frame") == 0)			//compare with "~". as initial cycle, declaer frame, field and line cycle.
				cycle = CYCLE_FRAME;
			else if(output_cycle.compare("field") == 0)
				cycle = CYCLE_FIELD;
			else if(output_cycle.compare("line") == 0)
				cycle = CYCLE_LINE;				//...
			else
			{
				ROS_ERROR("Unknown output_cycle value %s", output_cycle.c_str());		//arg 
				ros::shutdown();
			}

			driver.setTimeout(2.0);
			ROS_INFO("Connecting to %s", ip.c_str());		//initial print and Callback function
			driver.connect(ip.c_str(), port, 
					boost::bind(&hokuyo3d_node::cbConnect, this, _1));			//register functions and below
			driver.registerCallback(
					boost::bind(&hokuyo3d_node::cbPoint, this, _1, _2, _3, _4, _5, _6));
			driver.registerAuxCallback(
					boost::bind(&hokuyo3d_node::cbAux, this, _1, _2, _3, _4));
			driver.registerPingCallback(
					boost::bind(&hokuyo3d_node::cbPing, this, _1, _2));			//...
			field = 0;
			frame = 0;
			line = 0;

			sensor_msgs::ChannelFloat32 channel;
			channel.name = std::string("intensity");
			cloud.channels.push_back(channel);

			cloud2.height = 1;
			cloud2.is_bigendian = false;
			cloud2.is_dense = false;
			sensor_msgs::PointCloud2Modifier pc2_modifier(cloud2);
			pc2_modifier.setPointCloud2Fields(4,
					"x", 1, sensor_msgs::PointField::FLOAT32,
					"y", 1, sensor_msgs::PointField::FLOAT32,
					"z", 1, sensor_msgs::PointField::FLOAT32,
					"intensity", 1, sensor_msgs::PointField::FLOAT32);

			pubImu = nh.advertise<sensor_msgs::Imu>("imu", 5);
			pubMag = nh.advertise<sensor_msgs::MagneticField>("mag", 5);

			enablePc = enablePc2 = false;
			ros::SubscriberStatusCallback cbCon =
				boost::bind(&hokuyo3d_node::cbSubscriber, this);	//subscriber bind cbCon

			std::lock_guard<std::mutex> lock(connect_mutex);
			pubPc = nh.advertise<sensor_msgs::PointCloud>("hokuyo_cloud", 5, cbCon, cbCon);
			pubPc2 = nh.advertise<sensor_msgs::PointCloud2>("hokuyo_cloud2", 5, cbCon, cbCon);
		};
		~hokuyo3d_node()						//destructor
		{
			driver.requestAuxData(false);
			driver.requestData(true, false);
			driver.requestData(false, false);
			driver.poll();
			ROS_INFO("Communication stoped");
		};
		void cbSubscriber()						//subscriber
		{
			std::lock_guard<std::mutex> lock(connect_mutex);
			if(pubPc.getNumSubscribers() > 0)			//pubPc(adver) have data
			{
				enablePc = true;
				ROS_DEBUG("PointCloud output enabled");		//print
			}
			else
			{
				enablePc = false;
				ROS_DEBUG("PointCloud output disabled");
			}
			if(pubPc2.getNumSubscribers() > 0)
			{
				enablePc2 = true;
				ROS_DEBUG("PointCloud2 output enabled");
			}
			else
			{
				enablePc2 = false;
				ROS_DEBUG("PointCloud2 output disabled");
			}
		}
		bool poll()			//connection check
		{
			if(driver.poll())
			{
				return true;
			}
			ROS_ERROR("Connection closed");
			return false;
		};
		void ping()			//print ping
		{
			driver.requestPing();
			timePing = ros::Time::now();
		};
	private:
		ros::NodeHandle nh;
		ros::Publisher pubPc;
		ros::Publisher pubPc2;
		ros::Publisher pubImu;
		ros::Publisher pubMag;
		vssp::vsspDriver driver;
		sensor_msgs::PointCloud cloud;
		sensor_msgs::PointCloud2 cloud2;
		sensor_msgs::Imu imu;
		sensor_msgs::MagneticField mag;
				
		bool enablePc;
		bool enablePc2;
		std::mutex connect_mutex;

		ros::Time timePing;
		ros::Time timestampBase;

		int field;
		int frame;
		int line;

		enum
		{
			CYCLE_FIELD,
			CYCLE_FRAME,
			CYCLE_LINE
		} cycle;
		std::string ip;
		int port;
		int interlace;
		double range_min;
		std::string frame_id;
};


int main(int argc, char **argv)
{
	ros::init(argc, argv, "hokuyo3d");
	hokuyo3d_node node;

	ros::Rate wait(200);
	
	while (ros::ok())
	{
		if(!node.poll()) break;
		ros::spinOnce();
		//hokuyo3d_node::cbSubscriber;
		wait.sleep();
	}

	return 1;
}

