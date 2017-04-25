#ifndef QiaqiaMqttGateway_h
#define QiaqiaMqttGateway_h

#include <Arduino.h>   //该头文件内仅包含Energia.h文件
#include "Client.h"
#include <PubSubClient.h>  //第三方MQTT传输驱动库
#include <SPI.h>
#include <WiFi.h>
#include <MsgHandler.h>	//qiaqiacloud提供的MQTT协议解析包
#include "SLFS.h"  //第三方提供的文件管理包
#include <prcm.h>  
#include "HttpClient.h"  //第三方提供的HTTP协议包

#define SUBDEV_MAX_SIZE 20  //网关允许挂接子设备的最大数量
#define DATA_FUN_POINT 1000  //常规数据点和功能函数区分值：小于1000为数据点，大于1000为功能函数
#define GATEWAY_DATA_POINT 0  //常规数据点设置应大于的最小值。即数据点从1开始定义。
#define EPINDEX "cc3200"  //MQTT协议中必要字符串
#define EQUAL 0

#define AP_NAME "QQC_GW_TEST"  //热点模式下设备生成的网络名
#define AP_PASSWORD "qiaqiacloud818"  //热点模式下连接该网络密码


//调试时打开或关闭不必要的输出信息
#define DEVELOP_VERSION
#ifdef DEVELOP_VERSION
#define GATEWAY_DEBUG
#endif

#ifdef GATEWAY_DEBUG
#define GW_DBG_ln Serial.println    //打印并换行
#define GW_DBG Serial.print  //打印不换行
#else 
#define GW_DBG_ln
#define GW_DBG
#endif

//提供给开发者的子设备回调函数
//云端读回调函数：收到云端读命令后把需要的数据回传给云端
typedef void(*readcallback)(int busport, int devaddr, int offsetindex, char *ackdev);
//云端写回调函数：通过云端设置子设备
typedef void(*writecallback)(int busport, int devaddr, int offsetindex, float value);
//云端配置子设备系统时间
typedef void(*settimecallback)(int busport, int devaddr, char* datetime, char *ackdev);  
//云端重启子设备
typedef void(*setrestartcallback)(int busport, int devaddr);

typedef struct {
	char *epindex;
	char userid[15];
	bool gw_sub_successed;

	char gwindex[25];
	char apikey[25];
    char cloudip[20];
    char cloudport[6];
    char ssid[20];   //设备允许连接的wifi名长度
    char password[20];  //设备允许连接的wifi密码长度
} gateWayInfo;


typedef struct 
{
	char str_from[15];
	char str_ver[15];
	char str_srcuserid[25];
	char str_sflag[25];
	char str_epindex[25];
	char str_devindex[25];
	char str_subindex[25];
	char str_userid[15];
	char str_offsetindex[15];
	char str_cmd[15];
} topicParseInfo;

typedef struct 
{
	char devindex[25];
	char busport[15];
	char devaddr[15];
	char userid[15];
} subDevsInfo;

typedef struct 
{
	char busport[15];
	char devaddr[15];
	char offsetindex[15];
	char devack[80];
} gwReadCallbackParameters;

typedef struct 
{
	char busport[15];
	char devaddr[15];
	char offsetindex[15];
	char value[25];
} gwWriteCallbackParameters;

typedef struct 
{
	char offsetindex_src[15];
	char userid_des[25];
	char epindex_des[25];
	char devindex_des[25];
	char offsetindex_des[15];
	int subdev_num;
} gwAckInfo;

typedef struct 
{
	char updateHeader[150];  
	char updateIP[15];
	char updatePort[15];
	char updatePath[100];
} gwUpdateParameters;

class qiaqiaMqttGateway {
private:
	int _numofsubdevs;
	const char *_ackuseridcmd;   
	char _cloudip[20];
	char _cloudport[6];
    String _ackgwpayload[4];
	String _valuechangepayload[1];  

	char *apName;
	char *apPassword;
	uint8_t oldCountClients;
	uint8_t countClients;
	unsigned long apConfigStart;
	uint8_t apFirstTimeState;
	uint8_t RemoteFirstTimeState;
	unsigned long RemoteConfigStart;

  	gateWayInfo m_GwInfo;
	subDevsInfo m_SubDevsInfo[SUBDEV_MAX_SIZE];
	topicParseInfo m_topicParseInfo;
	gwReadCallbackParameters m_gwReadCallbackParameters;
	gwWriteCallbackParameters m_gwWriteCallbackParameters;
	gwAckInfo m_gwAckInfo;

	WiFiClient wifiClient;
	MsgHandler *pMsgHandler;
	PubSubClient *pClient;

	gwUpdateParameters m_gwUpdateParameters;  
	WiFiClient httpClient;  

  	void gwDataMemoryInit();   //clear结构体数据内容
  	int gwAckuseridSub();    //网关向云端订阅ackuserid
	int gwAckuseridRegist();  //向云端注册网关设备信息
	void gwTopicParse(char *topic);  //本地解析topic数据段
    void gwPayloadAckuseridParse(byte *payload, unsigned int length);  //本地解析payload数据段
    void gwRWSub();  //订阅网关的读写
	void gwSubdevsRWSub();  //订阅子设备读写

	void gwMQTTDataParse(char *topic, byte *payload, unsigned int length);  //接受云端传向来的数据包（topic+payload）

    gwReadCallbackParameters gwReadCallbackParametersDecode(byte *payload, unsigned int length);
	gwWriteCallbackParameters gwWriteCallbackParametersDecode(byte *payload, unsigned int length);
	gwAckInfo gwAckDecode(char *ackdev);
	void gwUpdateParametersDecode(byte *payload, unsigned int length); 
	int gwUpdateFirmware(char* cloudip, char* port, char* path); 
  
	bool gwWifiConfig(); 
	int gwUpdateStatus(String value); 
	void gwAPModeConfig();

public:
	qiaqiaMqttGateway();

	void gwInit();
	bool gwRun();
	int online(int busport, int devaddr, bool onlinestate);
	int gwValueChange(int busport, int devaddr, int offsetindex, String value);
	int gwAck(char *ackdev, String value);
	static void gwMQTTRecv(char *topic, byte* payload, unsigned int length, void *user);

	void setReadCallback(readcallback outerreadcall);
	readcallback m_ReadCallback;
	void setWriteCallback(writecallback outerwritecall);
	writecallback m_WriteCallback;
	void setGwAndDevTimeCallback(settimecallback outersettimecall);
	settimecallback m_SetTimeCallback;
	void setGwAndDevRestartCallback(setrestartcallback outersetrestartcall);
	setrestartcallback m_SetRestartCallback;
};

#endif