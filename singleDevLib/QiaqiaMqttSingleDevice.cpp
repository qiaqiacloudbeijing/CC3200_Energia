/*
 QiaqiaMQTT.h - the MQTT code for qiaqia cloud platform connection.
  Qiaqia Cloud
*/

#include "QiaqiaMqttSingleDevice.h"
#include <string.h>

QiaqiaMQTTSD::QiaqiaMQTTSD() {
	oldCountClients = 0;
	countClients = 0;

	readFileFlag = 0;

	apConfigStart = 0;
	apFirstTimeState = 1;
	
	RemoteFirstTimeState = 1;
    RemoteConfigStart = 0;

}

static void sdMQTTRecv(char *topic, byte* payload, unsigned int length, void *user) {
	QiaqiaMQTTSD* qiaqiaMQTTSD = (QiaqiaMQTTSD *)user;
	SD_DBG("Message arrived [");
  	SD_DBG(topic);
  	SD_DBG("] ");
  	for (int i = 0; i < length; i++) {
    	SD_DBG((char)payload[i]);
  	}

  	qiaqiaMQTTSD->sdMQTTDataParse(topic, payload, length);
}

void QiaqiaMQTTSD::sdDataMemoryInit() {
	memset(&m_topicParseInfo, 0, sizeof(topicParseInfo));
	memset(&m_sdReadCallbackParameters, 0, sizeof(sdReadCallbackParameters));
	memset(&m_sdWriteCallbackParameters, 0, sizeof(sdWriteCallbackParameters));
	
	memset(&m_sdAckInfo, 0, sizeof(sdAckInfo));
	memset(_cloudip, 0, 20);
	memset(_cloudport, 0, 6);
	memset(_acksdpayload, 0, 4);
}

void QiaqiaMQTTSD::sdInit(void){
	SD_DBG_ln("enter sdInit");
    
	sdDataMemoryInit();
	memset(&m_SdInfo, 0, sizeof(singleDeviceInfo));

	Serial.println("read singledevice parameters from flash file");
	if(readFileFlag == 0){
        SerFlash.begin();
        if(SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_READ) != SL_FS_OK){
          Serial.print("Error -- can't open file /storage/configinfo.bin, error code: ");
          Serial.println(SerFlash.lastErrorString());
          SerFlash.close();
          Serial.flush();  // flush pending serial output before entering suspend()
          Serial.println("please use APMode to config parameters");
        }
        else{
            Serial.println("read data from file");
            m_SdInfo.epindex = EPINDEX;
            SerFlash.readBytes(m_SdInfo.sdindex, sizeof(m_SdInfo.sdindex));
            Serial.println(m_SdInfo.sdindex);
            SerFlash.readBytes(m_SdInfo.apikey, sizeof(m_SdInfo.apikey));
            Serial.println(m_SdInfo.apikey);
            SerFlash.readBytes(m_SdInfo.cloudip, sizeof(m_SdInfo.cloudip));
            Serial.println(m_SdInfo.cloudip);
            SerFlash.readBytes(m_SdInfo.cloudport, sizeof(m_SdInfo.cloudport));
            Serial.println(m_SdInfo.cloudport);
            SerFlash.readBytes(m_SdInfo.ssid, sizeof(m_SdInfo.ssid));
            Serial.println(m_SdInfo.ssid);
            SerFlash.readBytes(m_SdInfo.password, sizeof(m_SdInfo.password));
            Serial.println(m_SdInfo.password);
            
            SerFlash.close();
            readFileFlag = 1;  
        }
    }

    //change char port to uint8_t port
    uint16_t port = atoi(m_SdInfo.cloudport);
    Serial.print("the port = ");
    Serial.println(port);

    apName = AP_NAME;
    apPassword = AP_PASSWORD;
    pClient = new PubSubClient(wifiClient);
	pClient->setServer(m_SdInfo.cloudip, port);
	pClient->setCallback(sdMQTTRecv, this);
}



void QiaqiaMQTTSD::sdAPModeConfig(void){
	Serial.println("Starting AP... ");

    WiFiServer MyServer(80);
    WiFi.beginNetwork(apName, apPassword);
    while (WiFi.localIP() == INADDR_NONE){
        Serial.print('.');
        delay(300);
    }
    Serial.println("DONE");
    
    Serial.print("LAN name = ");
    Serial.println(apName);
    Serial.print("WPA password = ");
    Serial.println(apPassword);
    
    IPAddress ip = WiFi.localIP();
    Serial.print("Webserver IP address = ");
    Serial.println(ip);
    Serial.print("Web-server port = ");
    MyServer.begin();     // start the web server on port 80
    Serial.println("80");
    Serial.println();
    uint8_t IsLoop = 0;  
    
    while(1)  
    {
        if(apFirstTimeState == 1){
          apConfigStart = millis();
          apFirstTimeState = 0;
        }
      
        if((millis() - apConfigStart) < 0){
            Serial.println("millis() runoff");  
            apFirstTimeState = 1;  
        }

        if(IsLoop == 0){
            if((millis() - apConfigStart) > 120000){  //访问AP模式的时间窗口是120s，超过
                Serial.println("restart the device");
                delay(1000);
                PRCMSOCReset();
            }
        }   

        if((millis() - apConfigStart) > 600000){  //进入AP config 超过10分钟就退出重启
            Serial.println("restart the device");
            delay(1000);
            PRCMSOCReset();
        }

        //判断是否有设备连接
        countClients = WiFi.getTotalDevices();  
        if (countClients != oldCountClients){
            if (countClients > oldCountClients)
            {  // Client connect
                // digitalWrite(RED_LED, !digitalRead(RED_LED));
                Serial.println("Client connected to AP");
                for (uint8_t k = 0; k < countClients; k++)
                {
                    Serial.print("Client #");
                    Serial.print(k);
                    Serial.print(" at IP address = ");
                    Serial.print(WiFi.deviceIpAddress(k));
                    Serial.print(", MAC = ");
                    Serial.println(WiFi.deviceMacAddress(k));
                    Serial.println("CC3200 in AP mode only accepts one client.");
                }
            }
            else
            {  // Client disconnect
                // digitalWrite(RED_LED, !digitalRead(RED_LED));
                Serial.println("Client disconnected from AP.");
                Serial.println();
            }
            oldCountClients = countClients;
        }

        //判断是否有数据传输
        WiFiClient myClient = MyServer.available();
        uint8_t resetflag = 0;  //0-默认 1-设置成功重启 2-不设置退出 3-设置失败

        if (myClient){   // if you get a client,
            Serial.println(". Client connected to server");  // print a message out the serial port
            char buffer[200] = {0};  // make a buffer to hold incoming data
            int8_t i = 0;
            resetflag = 0;           

            while (myClient.connected())
            {   // loop while the client's connected
                if (myClient.available())
                {    // if there's bytes to read from the client,
                    char c = myClient.read();   // read a byte, then
                    //Serial.write(c);    // print it out the serial monitor
                    if (c == '\n'){    // if the byte is a newline character
                    
                        // if the current line is blank, you got two newline characters in a row.
                        // that's the end of the client HTTP request, so send a response:
                        if (strlen(buffer) == 0)
                        {
                            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                            // and a content-type so the client knows what's coming, then a blank line:
                            myClient.println("HTTP/1.1 200 OK");
                            myClient.println("Content-type:text/html");
                            myClient.println();

                            //user's code  ---start---    author:Wei Yongshu & Yang Lili  @20170301
                            myClient.println("<!DOCTYPE html><html><head><meta charset='UTF-8'/><meta name='viewport', content='width=device-width, initial-scale=1, user-scalable=no'/><title>QIAQIA CLOUGATE</title>");
                            myClient.println("<style>body{margin:0;padding:0;background:rgb(19,67,131);font-size:16px;font-family:'微软雅黑'} p.row2,.row2{text-align:right;width:93%;padding-right:20px;}.row2 button{margin-bottom:16px;}.row2 a,.row p,#modify{color:#fff;display:inline-block;}#modify{margin-top:0;}.box{width:400px;margin:180px auto;}.box h2{text-align: center;color:#fff;} .row{width:100%;height:40px;margin-bottom:10px;box-sizing: border-box;}.row span{color:#fff;display: inline-block;width:120px;text-align: right;margin-right:12px} .row input{width: 240px;height:36px;font-size:18px;border:none;} .btn{display:inline-block;padding:6px 12px;background:#c7c7c7;color:#333;margin-left:20px;margin-top:20px;} .btn:visited,btn:link{background:blue;cursor:pointer;} .btn2{margin-left:220px} .msg{text-align:right;color:red;margin-right:20px;font-size:12px;}");
                            myClient.println("@media screen and (max-width:440px) {body {font-size: 14px;}.box {width: 320px;margin: 60px auto 0;}.row span {color: #fff;display: inline-block;width: 100px;text-align: right;margin-right: 12px}.row input {width: 188px;height: 36px;font-size: 15px}.btn2 {margin-left: 115px}} .info{color:#fff;padding:30px 10px;font-size:20px;text-align:center;}</style></head>");
                            myClient.println("<body>");


                            if (resetflag == 1) {
                                myClient.println("<div class='info'><p>配置成功，设备正在重启</p></div>");
                            } else if (resetflag == 2) {
                                myClient.println("<div class='info'><p>退出配置，设备正在重启</p></div>");
                            } else if (resetflag == 3) {
                                myClient.println("<div class='info'><p>配置失败，设备正在重启</p></div>");
                            } else{
                                myClient.println("<div class='box'><h2>设备参数配置</h2><div class='row'><span>Devindex:</span><input type='text' id='indextxt'/></div><p id='msg1' class='msg'></p><div class='row'><span>Apikey:</span><input type='text' id='keytxt'/></div><p id='msg2' class='msg'></p>");
                                myClient.println(" <div class='row'><span>Cloudip:</span><input type='text' id='iptxt'/></div><div class='row'><span>Port:</span><input type='text' id='porttxt'/></div><p id='msg3' class='msg'></p>");
                                myClient.println("<div class='row2'><p id='modify'>点击修改上述信息</p></div>");
                                myClient.println("<div id='accesswrap' class=' row2'><input type='text' id='access' />");
                                myClient.println("<p id='readmsg'></p>");
                                myClient.println("<button id='accessbtn'>确定</button>");
                                myClient.println("<button id='cancelmod'>取消修改</button></div>");
                                myClient.println("<div class='row'><span>wifi ssid:</span><input type='text' id='ssidtxt'/></div><p id='msg4' class='msg'></p>");
                                myClient.println("<div class='row'><span> wifi password: </span><input type='password' id='pwdtxt'/></div><p id='msg5' class='msg'></p>");
                                myClient.println("<div class=' row2'><a id='show'>点击显示密码</a>");
                                myClient.println("<a id='show2'>点击隐藏密码</a></div>");
                                myClient.println("<p id='msg' class='msg'></p><button class='btn btn2' id='sendcmd'>OK</button><button class='btn' onclick=\"location.href='/abandon'\">cancel</button></div>");
                                myClient.println("<script type='text/javascript'>");
                                myClient.println("window.onload = function() {");
                                myClient.println("var A,B,C,D,E,F;");
                                myClient.print("A='");
                                myClient.print(m_SdInfo.sdindex);
                                myClient.println("'");
                                myClient.print("B='");
                                myClient.print(m_SdInfo.apikey);
                                myClient.println("'");
                                myClient.print("C='");
                                myClient.print(m_SdInfo.cloudip);
                                myClient.println("'");
                                myClient.print("D='");
                                myClient.print(m_SdInfo.cloudport);
                                myClient.println("'");
                                myClient.print("E='");
                                myClient.print(m_SdInfo.ssid);
                                myClient.println("'");
                                myClient.print("F='");
                                myClient.print(m_SdInfo.password);
                                myClient.println("'");       

                                myClient.println("function id(d){return document.getElementById(d);};");
                                myClient.println("var indextxt = id('indextxt'), keytxt = id('keytxt'), iptxt = id('iptxt'), porttxt = id('porttxt'), sendcmd = id('sendcmd'),");
                                myClient.println(" msg = id('msg'), ssidtxt = id('ssidtxt'), pwdtxt = id('pwdtxt');");
                                myClient.println("var msg1 = id('msg1');");
                                myClient.println("var msg2 = id('msg2');");
                                myClient.println("var msg3 = id('msg3');");
                                myClient.println("var msg4 = id('msg4');");
                                myClient.println("var msg5 = id('msg5');");

                                myClient.println("indextxt.value = A;");
                                myClient.println("keytxt.value = B;");
                                myClient.println("iptxt.value = C;");
                                myClient.println("porttxt.value = D;");
                                myClient.println("ssidtxt.value = E;");
                                myClient.println("pwdtxt.value = F;");
                                myClient.println("var show = id('show');var show2 = id('show2');var modify = id('modify');show2.style.display='none';");
                                myClient.println("var access = id('access');var accesswrap = id('accesswrap');var accessbtn = id('accessbtn');");
                                myClient.println("var readmsg = id('readmsg');var cancelmod = id('cancelmod');accesswrap.style.display = 'none';");
                                myClient.println("if(indextxt.value&&keytxt.value&&iptxt.value&&porttxt.value){console.log('有值');indextxt.disabled = true;console.log(indextxt);");
                                myClient.println("keytxt.disabled = true;iptxt.disabled = true;porttxt.disabled = true;modify.style.display = 'inline-block';}else{");
                                myClient.println("console.log('有值');indextxt.disabled=false;keytxt.disabled=false;iptxt.disabled=false;porttxt.disabled=false;modify.style.display = 'none';console.log(indextxt);}");

                                myClient.println("modify.onclick = function (){accesswrap.style.display = 'block';access.value = '';modify.style.display = 'none';}");

                                myClient.println("accessbtn.onclick = function (){if(access.value&&access.value=='jiantongkeji'){readmsg.innerHTML = '';accesswrap.style.display = 'none';");
                                myClient.println("indextxt.disabled=false;keytxt.disabled=false;iptxt.disabled=false;porttxt.disabled=false;");
                                myClient.println("}else if(access.value&&access.value!='jiantongkeji'){readmsg.innerHTML = '密码错误，重新输入';}else if(!access.value){readmsg.innerHTML = '';}}");
                                myClient.println("cancelmod.onclick = function (){accesswrap.style.display = 'none';readmsg.innerHTML = '';");
                                myClient.println("modify.style.display = 'inline-block';access.value = '';}");
                                myClient.println("show.onclick = function () {pwdtxt.type = 'text';show2.style.display='inline-block';show.style.display='none';}");
                                myClient.println("show2.onclick = function () {pwdtxt.type = 'password';show.style.display='inline-block';show2.style.display='none';}");
                                
                                myClient.println("indextxt.onblur= function(){var devindex = indextxt.value;var reg =  /^[0-9a-zA-Z]*$/g;var re = new RegExp(reg);");
                                myClient.println("if(!re.test(devindex)||devindex.length!=24){");
                                myClient.println("msg1.innerHTML = '* devindex 输入等于24位的小写字母和数字的组合';}else{msg1.innerHTML = '';};}");

                                myClient.println("keytxt.onblur= function(){");
                                myClient.println("var apikey = keytxt.value;var reg =  /^[0-9a-zA-Z]*$/g;var re = new RegExp(reg);");
                                myClient.println("if(apikey.length!=24||!re.test(apikey)){msg2.innerHTML = '* apiKey 输入等于24位的小写字母和数字的组合';");
                                myClient.println("}else{msg2.innerHTML = '';}}");

                                myClient.println("porttxt.onblur= function(){");
                                myClient.println("var port = porttxt.value;var reg = /[0-9]/g;var re = new RegExp(reg);");
                                myClient.println("if(port.length>5||!re.test(port)){msg3.innerHTML = '* Port 输入范围 0~65535';");
                                myClient.println("}else{msg3.innerHTML = ''; }}");
                                
                                myClient.println("ssidtxt.onblur= function(){");
                                myClient.println("var wifissid = ssidtxt.value;var reg = /^[0-9a-zA-Z]*$/g;  var re = new RegExp(reg);");
                                myClient.println("if(wifissid.length>15||!re.test(wifissid)){msg4.innerHTML = '* wifissid 输入长度不超过15位的字母、数字或下划线的组合';");
                                myClient.println("}else{msg4.innerHTML = '';}}");
                                
                                myClient.println("pwdtxt.onblur= function(){");
                                myClient.println("var wifipassword = pwdtxt.value;var reg = /^[0-9a-zA-Z]*$/g;var re = new RegExp(reg);");
                                myClient.println("if(wifipassword.length>15||!re.test(wifipassword)){msg5.innerHTML = '* wifipassword 输入长度不超过15位的字母、数字或下划线的组合';");
                                myClient.println("}else{msg5.innerHTML = '';}}");
                                myClient.println("sendcmd.onclick = function() {");
                                myClient.println("var devindex = indextxt.value, apikey = keytxt.value, cloudip = iptxt.value, port = porttxt.value, wifissid = ssidtxt.value, wifipassword = pwdtxt.value;");
                               
                                myClient.println(" url = '/A=' + devindex + '&B=' + apikey + '&C=' + cloudip + '&D=' + port + '&E=' + wifissid + '&F=' + wifipassword+'&\';console.log(url);");
                                myClient.println("if (!devindex || !apikey || !cloudip || !port||!wifissid) {msg.innerHTML='请把信息补充完整'} else {msg.innerHTML='';location.href=url}};};");
                                myClient.println("</script>");
                            }

                            myClient.println("</body>");
                            //user's code  ---end---    author:Wei Yongshu & Yang Lili  @20170301


                            // The HTTP response ends with another blank line:
                            myClient.println();
                            // break out of the while loop:
                            IsLoop = 1;
                            break;
                        }
                        else
                        {  
                            // if you got a newline, then clear the buffer:
                            memset(buffer, 0, 200);
                            i = 0;
                        }
                    }
                    else if (c != '\r')
                    {    // if you got anything else but a carriage return character,
                        buffer[i++] = c;      // add it to the end of the currentLine
                    }
                 
                    //Serial.println(buffer);  //调试用
                    String text = buffer;

                    //解析收到的数据
                    if(text.endsWith("HTTP/1.")){
                        if(text.startsWith("GET /A")){ 

                            //摘取devindex的参数
                            memset(m_SdInfo.sdindex, 0 ,sizeof(m_SdInfo.sdindex));
                            char *pFrom_sdindex = strstr(buffer, "=");
                            char *pEnd_sdindex = strstr(buffer, "&");
                            strncpy(m_SdInfo.sdindex, pFrom_sdindex + 1, (uint8_t)(pEnd_sdindex - (pFrom_sdindex + 1)));
                             Serial.println(m_SdInfo.sdindex);

                            //摘取apikey的参数
                            memset(m_SdInfo.apikey, 0 ,sizeof(m_SdInfo.apikey));
                            char *pFrom_apikey = strstr(pFrom_sdindex + 1, "=");
                            char *pEnd_apikey = strstr(pEnd_sdindex + 1, "&");
                            strncpy(m_SdInfo.apikey, pFrom_apikey + 1, (uint8_t)(pEnd_apikey - (pFrom_apikey + 1)));
                            Serial.println(m_SdInfo.apikey);

                            //摘取cloudip的参数
                            memset(m_SdInfo.cloudip, 0 ,sizeof(m_SdInfo.cloudip));
                            char *pFrom_cloudip = strstr(pFrom_apikey + 1, "=");
                            char *pEnd_cloudip = strstr(pEnd_apikey + 1, "&");
                            strncpy(m_SdInfo.cloudip, pFrom_cloudip + 1, (uint8_t)(pEnd_cloudip - (pFrom_cloudip + 1)));
                            Serial.println(m_SdInfo.cloudip);

                            //摘取port的参数
                            memset(m_SdInfo.cloudport, 0 ,sizeof(m_SdInfo.cloudport));
                            char *pFrom_cloudport = strstr(pFrom_cloudip + 1, "=");
                            char *pEnd_cloudport = strstr(pEnd_cloudip + 1, "&");
                            strncpy(m_SdInfo.cloudport, pFrom_cloudport + 1, (uint8_t)(pEnd_cloudport - (pFrom_cloudport + 1)));
                            Serial.println(m_SdInfo.cloudport);

                            //摘取wifi_ssid的参数
                            memset(m_SdInfo.ssid, 0 ,sizeof(m_SdInfo.ssid));
                            char *pFrom_ssid = strstr(pFrom_cloudport + 1, "=");
                            char *pEnd_ssid = strstr(pEnd_cloudport + 1, "&");
                            strncpy(m_SdInfo.ssid, pFrom_ssid + 1, (uint8_t)(pEnd_ssid - (pFrom_ssid + 1)));
                            Serial.println(m_SdInfo.ssid);

                            //摘取wifi_password的参数
                            memset(m_SdInfo.password, 0 ,sizeof(m_SdInfo.password));
                            char *pFrom_password = strstr(pFrom_ssid + 1, "=");
                            char *pEnd_password = strstr(pEnd_ssid + 1, "&");
                            strncpy(m_SdInfo.password, pFrom_password + 1, (uint8_t)(pEnd_password - (pFrom_password + 1)));
                            Serial.println(m_SdInfo.password);

                            //把各参数写入文件
                            SerFlash.del("/storage/configinfo.bin");
                            int32_t retval = SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_CREATE(256, _FS_FILE_OPEN_FLAG_COMMIT));
                            if(retval == SL_FS_OK){
                                SerFlash.write(m_SdInfo.sdindex, sizeof(m_SdInfo.sdindex));
                                SerFlash.write(m_SdInfo.apikey, sizeof(m_SdInfo.apikey));
                                SerFlash.write(m_SdInfo.cloudip, sizeof(m_SdInfo.cloudip));
                                SerFlash.write(m_SdInfo.cloudport, sizeof(m_SdInfo.cloudport));
                                SerFlash.write(m_SdInfo.ssid, sizeof(m_SdInfo.ssid));
                                SerFlash.write(m_SdInfo.password, sizeof(m_SdInfo.password));
                          
                                SerFlash.close();
                                Serial.println("restart the device");

                                resetflag = 1;
                            }
                            else{
                                // retval did not return SL_FS_OK, there must be an error!
                                Serial.print("Error opening /storage/configinfo.bin, error code: ");
                                Serial.println(SerFlash.lastErrorString());
                                Serial.flush();  // flush pending serial output before entering suspend()
                                Serial.print("please reset the device！！");
                                resetflag = 3;
                            }
                        }
                        if(text.startsWith("GET /abandon")){
                            Serial.println("restart the device");
                            resetflag = 2;
                        }
                    }
                }
            }
               
            // close the connection:
            myClient.stop();
            Serial.println(". Client disconnected from server");
            Serial.println();            
        } 

        if(resetflag != 0){
            delay(1000);
            PRCMSOCReset();
        }          
    } 
}

bool QiaqiaMQTTSD::sdWifiConfig(void){
    Serial.println("if not connecting wifi in 20 seconds, then start wificonfig");
    
    if(m_SdInfo.password[0] == '\0'){
        WiFi.begin(m_SdInfo.ssid);
    }else {
        WiFi.begin(m_SdInfo.ssid, m_SdInfo.password);
    }
        for(int i = 0; i<40; i++){
          if(WiFi.status() == WL_CONNECTED){
             Serial.println("auto connect wifi succeed");
             return true;
          } 
           Serial.print(".");
           delay(500);
        }  
        
    Serial.println("auto connecting failed");

    Serial.println("enter APMode to config wifi parameters");
    sdAPModeConfig();

    return false;  
}

bool QiaqiaMQTTSD::sdRun() {
	SD_DBG_ln("enter gwRun");

    //wifi连接任务
	if(WiFi.status() != WL_CONNECTED){
    	Serial.println("wifi disconnected, start wifi Connection");
		sdWifiConfig();
    }    
    Serial.println("WiFi Connection success");

	if (WiFi.status() == WL_CONNECTED) {
		if (pClient && !pClient->connected()) {
			SD_DBG_ln("Disconnected Reconnecting....");

			randomSeed(millis());  //随机数种子
			Serial.println(millis()%1000);
			char clientID[50] = {0};
			sprintf(clientID, "CC3200%d", random(1,1000)); 
			
			m_SdInfo.sd_sub_successed = false;
			sdDataMemoryInit();

			if(!pClient->connect(clientID, m_SdInfo.sdindex, m_SdInfo.apikey)) {
				SD_DBG("Connection failed");
				
				if(RemoteFirstTimeState == 1){
                    RemoteConfigStart = millis();
                    RemoteFirstTimeState = 0;
                }
      
                if((millis() - RemoteConfigStart) < 0){
                    Serial.println("millis() runoff");  //按下时正赶上溢出
                    RemoteFirstTimeState = 1;  //下次重新标记按下时刻
                }

                if((millis() - RemoteConfigStart) > 60000){  //连接云端服务器超时时间60s，之后进人AP配置
                    Serial.println("enter APMode to config wifi parameters");
                    sdAPModeConfig();
                }
			} else {
				RemoteFirstTimeState = 1;
                RemoteConfigStart = 0;
				sdAckuseridSub();
				sdAckuseridRegist();
				SD_DBG_ln("Connection success");
			}

		}
		pClient->loop();
	}
    
	return true;
}

int QiaqiaMQTTSD::sdAckuseridSub() {
	SD_DBG_ln("enter sdAckuseridSub");

	return pClient->subscribe(pMsgHandler->topicAckuseridEncode(m_SdInfo.epindex, m_SdInfo.sdindex));
}

int QiaqiaMQTTSD::sdAckuseridRegist() {
	SD_DBG_ln("enter sdAckuseridRegist");

	return pClient->publish(pMsgHandler->topicRegisterEncode(m_SdInfo.epindex, m_SdInfo.sdindex), "");
}

void QiaqiaMQTTSD::sdTopicParse(char *topic) {
	SD_DBG_ln("enter sdTopicParse");

	uint16_t topic_len = strlen(topic);
	memset(&m_topicParseInfo, 0, sizeof(topicParseInfo));

	SD_DBG("Message arrived [");
  	SD_DBG(topic);
  	SD_DBG_ln("] ");

	int len = 0;
	char *pfrom = strstr(topic, "/");
	if(pfrom != NULL)
		strncpy(m_topicParseInfo.str_from, (char *)topic, (uint8_t)(pfrom - topic));
	if(strlen(m_topicParseInfo.str_from) == 0) return;
	len += (strlen(m_topicParseInfo.str_from) + 1);
	SD_DBG(m_topicParseInfo.str_from);

	char *pver = strstr(pfrom + 1, "/");
  	if(pver != NULL)
    	strncpy(m_topicParseInfo.str_ver, (char *)pfrom + 1, (uint8_t)(pver - pfrom - 1));
  	if(strlen(m_topicParseInfo.str_ver) == 0) return;
  	len += (strlen(m_topicParseInfo.str_ver) + 1);
  	SD_DBG(m_topicParseInfo.str_ver);

  	char *psrcuserid = strstr(pver + 1, "/");
  	if(psrcuserid != NULL)
    	strncpy(m_topicParseInfo.str_srcuserid, (char *)pver + 1, (uint8_t)(psrcuserid - pver - 1));
  	if(strlen(m_topicParseInfo.str_srcuserid) == 0) return;
  	len += (strlen(m_topicParseInfo.str_srcuserid) + 1);
  	SD_DBG(m_topicParseInfo.str_srcuserid);

  	char *psflag = strstr(psrcuserid + 1, "/");
  	if(psflag != NULL)
    	strncpy(m_topicParseInfo.str_sflag, (char *)psrcuserid + 1, (uint8_t)(psflag - psrcuserid - 1));
  	if(strlen(m_topicParseInfo.str_sflag) == 0) return;
  	len += (strlen(m_topicParseInfo.str_sflag) + 1);
  	SD_DBG(m_topicParseInfo.str_sflag);

  	char *pepindex = strstr(psflag + 1, "/");
  	if(pepindex != NULL)
    	strncpy(m_topicParseInfo.str_epindex, (char *)psflag + 1, (uint8_t)(pepindex - psflag - 1));
  	if(strlen(m_topicParseInfo.str_epindex) == 0) return;
  	len += (strlen(m_topicParseInfo.str_epindex) + 1);
  	SD_DBG(m_topicParseInfo.str_epindex);

  	char *pdevindex = strstr(pepindex + 1, "/");
  	if(pdevindex != NULL)
    	strncpy(m_topicParseInfo.str_devindex, (char *)pepindex + 1, (uint8_t)(pdevindex - pepindex - 1));
  	if(strlen(m_topicParseInfo.str_devindex) == 0) return;
  	len += (strlen(m_topicParseInfo.str_devindex) + 1);
  	SD_DBG(m_topicParseInfo.str_devindex);

  	char *psubindex = strstr(pdevindex + 1, "/");
  	if(psubindex != NULL)
    	strncpy(m_topicParseInfo.str_subindex, (char *)pdevindex + 1, (uint8_t)(psubindex - pdevindex - 1));
  	if(strlen(m_topicParseInfo.str_subindex) == 0) return;
  	len += (strlen(m_topicParseInfo.str_subindex) + 1);
  	SD_DBG(m_topicParseInfo.str_subindex);

  	char *puserid = strstr(psubindex + 1, "/");
  	if(puserid != NULL)
    	strncpy(m_topicParseInfo.str_userid, (char *)psubindex + 1, (uint8_t)(puserid - psubindex - 1));
  	if(strlen(m_topicParseInfo.str_userid) == 0) return;
  	len += (strlen(m_topicParseInfo.str_userid) + 1);
  	SD_DBG(m_topicParseInfo.str_userid);

  	char *poffsetindex = strstr(puserid + 1, "/");
  	if(poffsetindex != NULL)
    	strncpy(m_topicParseInfo.str_offsetindex, (char *)puserid + 1, (uint8_t)(poffsetindex - puserid - 1));
  	if(strlen(m_topicParseInfo.str_offsetindex) == 0) return;
  	len += (strlen(m_topicParseInfo.str_offsetindex) + 1);
  	SD_DBG(m_topicParseInfo.str_offsetindex);

  	char *pcmd = poffsetindex + 1;
  	if((pcmd != NULL) && (len < topic_len))
    	strncpy(m_topicParseInfo.str_cmd, (char *)poffsetindex + 1, (size_t)(topic_len -len));
  	if(strlen(m_topicParseInfo.str_cmd) == 0) return;
  	SD_DBG(m_topicParseInfo.str_cmd);
}

void QiaqiaMQTTSD::sdRWSub() {
	SD_DBG_ln("sdRWSub");

	pClient->subscribe(pMsgHandler->topicReadEncode(m_SdInfo.sdindex, m_SdInfo.userid));
	pClient->subscribe(pMsgHandler->topicWriteEncode(m_SdInfo.sdindex, m_SdInfo.userid));
}

void QiaqiaMQTTSD::sdPayloadAckuseridParse(byte *payload, unsigned int length) {
	SD_DBG_ln("enter sdPayloadAckuseridParse");

	for (int i = 0; i < length; i++) {
    	SD_DBG((char)payload[i]);
  	}
    SD_DBG_ln();
  	uint8_t ackuserid_msg_type;

  	DynamicJsonBuffer jsonBufferAckuserid;
  	JsonObject& root = jsonBufferAckuserid.parseObject((char *)payload);

  	if(!root.success()) {
		Serial.println("Json format wrong");
		return;
	}

	int count = 0;
	_ackuseridcmd = root["cmd"];
	const char *userid = root["userid"];

	const char *ip = root["server"]["ip"];
	const char *port = root["server"]["port"];

	ackuserid_msg_type = pMsgHandler->mqtt_get_ackuserid_type(_ackuseridcmd);

	switch (ackuserid_msg_type) {
	case CMD_MSG_TYPE_OK:
		strncpy(m_SdInfo.userid, (char *)userid, strlen(userid));
		m_SdInfo.sd_sub_successed = true;
		sdRWSub();
		break;
	case CMD_MSG_TYPE_REDIRECT:
    {
		strncpy(_cloudip, (char *)ip, strlen(ip));
		strncpy(_cloudport, (char *)port, strlen(port));
		
        
            int32_t retvall = SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_WRITE);  //打开文件
            if(retvall == SL_FS_OK){
                SerFlash.write(m_SdInfo.sdindex, sizeof(m_SdInfo.sdindex));
                SerFlash.write(m_SdInfo.apikey, sizeof(m_SdInfo.apikey));
                SerFlash.write(_cloudip, sizeof(_cloudip));
                SerFlash.write(_cloudport, sizeof(_cloudport));
                SerFlash.write(m_SdInfo.ssid, sizeof(m_SdInfo.ssid));
                SerFlash.write(m_SdInfo.password, sizeof(m_SdInfo.password));
            }
            SerFlash.close();

            delay(100);
            PRCMSOCReset();
    }
		break;
	default:
		break;
	}
}

int QiaqiaMQTTSD::sdValueChange(int offsetindex, String value) {
	SD_DBG_ln("enter gwValueChange");

	static char offset[24] = {"\0"};  
	memset(offset, 0, 24);
	if (m_SdInfo.sd_sub_successed == true) {
		Serial.println("enter valuechange publish!!");
		_valuechangepayload[0] = value;
		sprintf(offset, "%d", offsetindex);
		return pClient->publish(pMsgHandler->topicValuechangeEncode(m_SdInfo.userid, m_SdInfo.epindex, m_SdInfo.sdindex, m_SdInfo.userid, offset), 
							pMsgHandler->payloadJsonEncode(pMsgHandler->payload_encode_type("valuechange"), _valuechangepayload));
			
		}
}


sdWriteCallbackParameters QiaqiaMQTTSD::sdWriteCallbackParametersDecode(byte *payload, unsigned int length) {
	SD_DBG_ln("enter sdWriteCallbackParametersDecode");

	sdWriteCallbackParameters gwwcp;

	DynamicJsonBuffer jsonBufferWrite;
	JsonObject& root = jsonBufferWrite.parseObject((char *)payload);

	if(!root.success()) {
		Serial.println("Json format wrong");
	}

	const char *sflag_write = root["data"]["devid"]["sflag"];
	const char *epindex_write = root["data"]["devid"]["epindex"];
	const char *devindex_write = root["data"]["devid"]["devindex"];
	const char *subindex_write = root["data"]["devid"]["subindex"];
	const char *offsetindex_write = root["data"]["offsetindex"];
	const char *value_write = root["data"]["value"];

	strncpy(gwwcp.offsetindex, m_topicParseInfo.str_offsetindex, strlen(m_topicParseInfo.str_offsetindex));
	strncpy(gwwcp.value, value_write, strlen(value_write));
			
	return gwwcp;
}


sdReadCallbackParameters QiaqiaMQTTSD::sdReadCallbackParametersDecode(byte *payload, unsigned int length) {
  	SD_DBG_ln("enter sdReadCallbackParametersDecode");

  	for (int i = 0; i < length; i++) {
    	SD_DBG((char)payload[i]);
  	}

    SD_DBG_ln();
	sdReadCallbackParameters gwrcp;

	DynamicJsonBuffer jsonBufferRead;
	JsonObject& root = jsonBufferRead.parseObject((char *)payload);

	if(!root.success()) {
		SD_DBG("Json format wrong");
	}

	const char *sflag_read = root["devid"]["sflag"];  
	const char *epindex_read = root["devid"]["epindex"];
	const char *devindex_read = root["devid"]["devindex"];
	const char *subindex_read = root["devid"]["subindex"];
	const char *offsetindex_read = root["offsetindex"];
	const char *value_read = root["data"]["value"];

	strncpy(gwrcp.offsetindex, m_topicParseInfo.str_offsetindex, strlen(m_topicParseInfo.str_offsetindex));
	char *devacktemp = pMsgHandler->gwAckReadEncode(m_topicParseInfo.str_srcuserid, (char *)devindex_read,
									(char *)offsetindex_read, m_topicParseInfo.str_offsetindex, (char *)epindex_read); 
	
	strncpy(gwrcp.devack, devacktemp, strlen(devacktemp));
		
	return gwrcp;
}

void QiaqiaMQTTSD::sdUpdateParametersDecode(byte *payload, unsigned int length){
	SD_DBG_ln("enter sdUpdateParametersDecode");

  	for (int i = 0; i < length; i++) {
    	SD_DBG((char)payload[i]);
  	}
    SD_DBG_ln();

	DynamicJsonBuffer jsonBufferRead;
	JsonObject& root = jsonBufferRead.parseObject((char *)payload);

	if(!root.success()) {
		SD_DBG("Json format wrong");
	}

	const char *date_value = root["data"]["value"];  //把升级路径提取出来
	const char *date_host = root["data"]["host"];
	const char *date_port = root["data"]["port"];
	const char *date_path = root["data"]["path"]; 
	
	memset(&m_sdUpdateParameters, 0, sizeof(m_sdUpdateParameters));

	SD_DBG("update path [");
  	SD_DBG(date_value);
  	SD_DBG_ln("] ");

	strncpy(m_sdUpdateParameters.updateIP, (char*)date_host, strlen(date_host));
	strncpy(m_sdUpdateParameters.updatePort, (char*)date_port, strlen(date_port));
	strncpy(m_sdUpdateParameters.updatePath, (char*)date_path, strlen(date_path));
}

sdAckInfo QiaqiaMQTTSD::sdAckDecode(char *ackdev) {
	SD_DBG_ln("enter sdAckDecode");

	sdAckInfo gai;
	memset(&gai, 0, sizeof(sdAckInfo));

	char *pdesuserid = strstr(ackdev, "/");
	if (pdesuserid != NULL)
		strncpy(gai.userid_des, (char *)ackdev, (uint8_t)(pdesuserid - ackdev));
	if (strlen(gai.userid_des) == 0) return gai;

	char *pdevindex = strstr(pdesuserid + 1, "/");
	if (pdevindex != NULL)
		strncpy(gai.devindex_des, (char *)pdesuserid + 1, (uint8_t)(pdevindex- pdesuserid - 1));
	if (strlen(gai.devindex_des) == 0) return gai;

	char *pdesoffsetindex = strstr(pdevindex + 1, "/");
	if (pdesoffsetindex != NULL)
		strncpy(gai.offsetindex_des, (char *)pdevindex + 1, (uint8_t)(pdesoffsetindex - pdevindex - 1));
	if (strlen(gai.offsetindex_des) == 0) return gai;

	char *psrcoffsetindex = strstr(pdesoffsetindex + 1, "/");
	if (psrcoffsetindex != NULL)
		strncpy(gai.offsetindex_src, (char *)pdesoffsetindex + 1, (uint8_t)(psrcoffsetindex - pdesoffsetindex - 1));
	if (gai.offsetindex_src == 0) return gai;

	char *pepindex = strstr(psrcoffsetindex + 1, "/");
	if (pepindex != NULL)
		strncpy(gai.epindex_des, (char *)psrcoffsetindex + 1, (uint8_t)(pepindex - psrcoffsetindex - 1));
	if (gai.epindex_des == 0) return gai;

	return gai;
}

int QiaqiaMQTTSD::sdAck(char *ackdev, String value) {
	SD_DBG_ln("enter sdAck");

	if (m_SdInfo.sd_sub_successed == true) {
		m_sdAckInfo = sdAckDecode(ackdev);

		_acksdpayload[0] = m_SdInfo.epindex;
		_acksdpayload[1] = m_SdInfo.sdindex;  
		_acksdpayload[2] = m_sdAckInfo.offsetindex_src;
		_acksdpayload[3] = value;

		return pClient->publish(pMsgHandler->topicAckEncode(m_SdInfo.userid, m_sdAckInfo.epindex_des, m_sdAckInfo.devindex_des, m_sdAckInfo.userid_des, m_sdAckInfo.offsetindex_des), 
					pMsgHandler->payloadJsonEncode(pMsgHandler->payload_encode_type("devack"), _acksdpayload));
	}
}

int QiaqiaMQTTSD::sdUpdateFirmware(char* cloudip, char* port, char* path){
	int Readbyte;
	int32_t readStatus;
	int i,j;
	int endloop;
	long prog_file_size;

	readStatus = SerFlash.open("\\sys\\mcuimg.bin", FS_MODE_OPEN_READ);
	if(readStatus == SL_FS_OK){
		Serial.println("--Found Flash file = ");
		SerFlash.close();
	} else{
			Serial.print("Error opening \\sys\\mcuimg.bin error code: ");
    		Serial.println(SerFlash.lastErrorString());
    		SerFlash.close();// close the file
        	return (0);
		}

	readStatus=SerFlash.del("\\sys\\mcuimg.bin");
    Serial.print("Deleting \\sys\\mcuimg.bin return code: ");
    Serial.println(SerFlash.lastErrorString());

    readStatus=SerFlash.open("\\sys\\mcuimg.bin", FS_MODE_OPEN_CREATE(100*1024, _FS_FILE_OPEN_FLAG_COMMIT));  //max=128k
    if (readStatus != SL_FS_OK){
    	Serial.print("Error creating file \\sys\\mcuimg.bin, error code: ");
    	Serial.println(SerFlash.lastErrorString());
    	SerFlash.close();// close the file
        return (0);
    } else {
    	Serial.print("File created: \\sys\\mcuimg.bin");
      	SerFlash.close();
    	}

    readStatus=SerFlash.open("\\sys\\mcuimg.bin", FS_MODE_OPEN_WRITE);
    if (readStatus != SL_FS_OK){
    	Serial.print("Err file \\sys\\mcuimg.bin for write,Err-");
    	Serial.println(SerFlash.lastErrorString());
    	SerFlash.close();
   		return (0);
    }
    Serial.print("Flash File Opened for wr: ");
    Serial.println(SerFlash.lastErrorString());

    int err = 0;
  	HttpClient http(httpClient); 

  	err = http.get(cloudip, (unsigned int)(atol(port)), path);

  	if(err == 0){
  		Serial.println("startedRequest ok");
    	err = http.responseStatusCode();
    	if (err >= 0){
    		Serial.print("Got status code: ");
      		Serial.println(err);
      		err = http.skipResponseHeaders();
      		if (err >= 0){
      			int bodyLen = http.contentLength();
      			int body_onepart = bodyLen/4; 
      			Serial.print("Content length is: ");
        		Serial.println(bodyLen);
        		Serial.println();
        		Serial.println("Body returned follows:");

				sdUpdateStatus("0"); 

        		unsigned long timeoutStart = millis();
        		char c;

        		while ((http.connected() || http.available()) && ((millis() - timeoutStart) < 30*1000)){
        			if (http.available()){
        				c = http.read();
                		if((readStatus = SerFlash.write(c)) == 0){
                			Serial.print("Error flash Wr-");
                    		Serial.println(SerFlash.lastErrorString());
                    		SerFlash.close();
                		}
                		//Serial.print(c); //测试用
                		bodyLen--;

                		if(bodyLen == 3*body_onepart){
                            Serial.println("update 25");
                			sdUpdateStatus("25");
                		}
                	    if(bodyLen == 2*body_onepart){
                            Serial.println("update 50");
                			sdUpdateStatus("50");
                		}
                		if(bodyLen == body_onepart){
                            Serial.println("update 75");
                			sdUpdateStatus("75");
                		}

                		timeoutStart = millis();
        			}
        			else 
        			{
        				delay(1000);
        			}
      			}
      		}	
      		else 
      		{
      			Serial.print("Failed to skip response headers: ");
        		Serial.println(err);
      		}
      		
        }
        else
        {
        	Serial.print("Getting response failed: ");
      		Serial.println(err);
      		return 0;
        }
  	}
  	else
  	{
  		Serial.print("Connect failed: ");
    	Serial.println(err);
    	return 0;
  	}
  	http.stop();
  	SerFlash.close();

  	sdUpdateStatus("100");  
    Serial.println("update ok");
  	delay(2000);  

  	PRCMSOCReset();  	
}

void QiaqiaMQTTSD::sdMQTTDataParse(char *topic, byte *payload, unsigned int length) {
	SD_DBG_ln("enter sdMQTTDataParse");

	uint8_t msg_type;

	sdTopicParse(topic);

	msg_type = pMsgHandler->mqtt_get_type(m_topicParseInfo.str_cmd);
	
	switch (msg_type) {
	case MQTT_MSG_TYPE_ACKUSERID:
		sdPayloadAckuseridParse(payload, length);
		break;
	case MQTT_MSG_TYPE_READ:
		if (m_SdInfo.sd_sub_successed == true) {
			if (atol(m_topicParseInfo.str_offsetindex) <= DATA_FUN_POINT) {
				m_sdReadCallbackParameters = sdReadCallbackParametersDecode(payload, length);
				if (m_ReadCallback) {
					m_ReadCallback(atol(m_sdReadCallbackParameters.offsetindex), m_sdReadCallbackParameters.devack);
				}
			} else if (atol(m_topicParseInfo.str_offsetindex) > DATA_FUN_POINT) {
				switch (atol(m_topicParseInfo.str_offsetindex)) {
				case FUN_MSG_TYPE_READ_DEV_ID:
					/*
					m_sdReadCallbackParameters = sdReadCallbackParametersDecode(payload, length); 
					if (m_ReadCallback) {
						m_ReadCallback(atol(m_sdReadCallbackParameters.busport), atol(m_sdReadCallbackParameters.devaddr),
										atol(m_sdReadCallbackParameters.offsetindex), m_sdReadCallbackParameters.devack);
						} */
					break;

				case FUN_MSG_TYPE_MODEL_ID:
				/*
					m_sdReadCallbackParameters = sdReadCallbackParametersDecode(payload, length); 
					if (m_ReadCallback) {
						m_ReadCallback(atol(m_sdReadCallbackParameters.busport), atol(m_sdReadCallbackParameters.devaddr),
										atol(m_sdReadCallbackParameters.offsetindex), m_sdReadCallbackParameters.devack);
						} */
					break;

				case FUN_MSG_TYPE_READ_TIME:   
					/*
                    {
					m_sdReadCallbackParameters = sdReadCallbackParametersDecode(payload, length);
                    /*
					unsigned long temp_seconds = 0;
					unsigned long temp_seconds_8 = 0;
					char strTime[50] = {0};

					PRCMRTCGet(&temp_seconds, 0);  //拿到当前设备的unixstamp
					Serial.println(temp_seconds);

					temp_seconds_8 = temp_seconds + 8*60*60;  
					unixTime2Str(temp_seconds_8, strTime, sizeof(strTime));

					gwAck(m_sdReadCallbackParameters.devack, strTime);
					Serial.println(strTime);
                    
					}
                    */
					break;

				case FUN_MSG_TYPE_UPDATE_STATUS_F:  
					break;
				case FUN_MSG_TYPE_UPDATE_STATUS_S:  
					break;
				case FUN_MSG_TYPE_DEBUG_SWITCHER_R:
					break;
				case FUN_MSG_TYPE_DEBUG_INFO:
					break;
				default:
					break;
				}
			}
		}
		break;
	case MQTT_MSG_TYPE_WRITE:
		if (m_SdInfo.sd_sub_successed == true) {
			if (atol(m_topicParseInfo.str_offsetindex) <= DATA_FUN_POINT) {
				m_sdWriteCallbackParameters = sdWriteCallbackParametersDecode(payload, length);
				if (m_WriteCallback) {
					m_WriteCallback(atol(m_sdWriteCallbackParameters.offsetindex), atof(m_sdWriteCallbackParameters.value));
				}
			} else if (atol(m_topicParseInfo.str_offsetindex) > DATA_FUN_POINT) {
				switch (atol(m_topicParseInfo.str_offsetindex)) {
				case FUN_MSG_TYPE_DEV_RESTART:
					SD_DBG_ln("enter singledevice restart callback");
    				delay(1000);
   	 				PRCMSOCReset();  //Performs a software reset of a SOC
					break;

				case FUN_MSG_TYPE_ADJUST_TIME:
                    /*
					{  //在case语句中有局部变量，case前后要加{}
					m_sdWriteCallbackParameters = sdWriteCallbackParametersDecode(payload, length);
					Serial.println("enter adjust time!!");
					Serial.println(m_sdWriteCallbackParameters.offsetindex);
					Serial.println(m_sdWriteCallbackParameters.value);
					
					time_t unix_utc_8 = 0;  //time_t = long  

					//date-->unixstamp
					unix_utc_8 = strTime2unix(m_sdWriteCallbackParameters.value) - 8*60*60;
					//unix_utc_8 = strTime2unix(m_sdWriteCallbackParameters.value); //将收到的时间字符串转化为unix，utc时间
					Serial.println(unix_utc_8);
					PRCMRTCSet(unix_utc_8, 0);  //设置设备时间
					}
                    */
					break;

				case FUN_MSG_TYPE_UPDATE_F:
					SD_DBG_ln("enter update!!");
					sdUpdateParametersDecode(payload, length);
					sdUpdateFirmware(m_sdUpdateParameters.updateIP, m_sdUpdateParameters.updatePort, m_sdUpdateParameters.updatePath);
					break;

				case FUN_MSG_TYPE_UPDATE_S:
					break;
				case FUN_MSG_TYPE_DEBUG_SWITCHER_W:
					break;

				case FUN_MSG_TYPE_WIFI_CLEAR:
                {
					SD_DBG_ln("enter wifi clear");
                    memset(m_SdInfo.ssid, 0, sizeof(m_SdInfo.ssid));
                    memset(m_SdInfo.password, 0 ,sizeof(m_SdInfo.password));
                    int32_t retvall = SerFlash.open("/storage/configinfo.bin", FS_MODE_OPEN_WRITE);  //打开文件
                    if(retvall == SL_FS_OK){
                        SerFlash.write(m_SdInfo.sdindex, sizeof(m_SdInfo.sdindex));
                        SerFlash.write(m_SdInfo.apikey, sizeof(m_SdInfo.apikey));
                        SerFlash.write(m_SdInfo.cloudip, sizeof(m_SdInfo.cloudip));
                        SerFlash.write(m_SdInfo.cloudport, sizeof(m_SdInfo.cloudport));
                        SerFlash.write(m_SdInfo.ssid, sizeof(m_SdInfo.ssid));
                        SerFlash.write(m_SdInfo.password, sizeof(m_SdInfo.password));
                    }
                    SerFlash.close();

                    delay(100);
                    PRCMSOCReset();
                }
					
					break;
				default:
					break; 
				}
			}
		}
		break;
	default:
		break;
	}
}

void QiaqiaMQTTSD::setReadCallback(readcallback outerreadcall) {
	m_ReadCallback = outerreadcall;
}

void QiaqiaMQTTSD::setWriteCallback(writecallback outerwritecall) {
	m_WriteCallback = outerwritecall;
}

int QiaqiaMQTTSD::sdUpdateStatus(String value) {
	SD_DBG_ln("enter update status");

	if (m_SdInfo.sd_sub_successed == true) {
		_valuechangepayload[0] = value;	
		return pClient->publish(pMsgHandler->topicUpdateStatusEncode(m_SdInfo.userid, m_SdInfo.epindex, m_SdInfo.sdindex, m_SdInfo.userid, "1005"), 
						            pMsgHandler->payloadJsonEncode(4, _valuechangepayload));
	}
}

/*unix时间戳
void QiaqiaMQTTGW::gwRTCInit(){
	PRCMRTCInUseSet();  //启动设备的RTC
  	//PRCMRTCSet(0, 0);   //设置为初始值：1970-1-1 00:00:00
}

time_t QiaqiaMQTTGW::strTime2unix(char* timeStamp){
	struct tm tm;
  	memset(&tm, 0 ,sizeof(tm));

  	sscanf(timeStamp, "%d-%d-%d %d:%d:%d",
          &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
          &tm.tm_hour, &tm.tm_min, &tm.tm_sec);

  	tm.tm_year -= 1900;
  	tm.tm_mon--;

  	return mktime(&tm);
}

void QiaqiaMQTTGW::unixTime2Str(int n, char strTime[], int bufLen){
   struct tm tm = *localtime((time_t *)&n);
   strftime(strTime, bufLen - 1, "%Y-%m-%d %H:%M:%S", &tm);
   strTime[bufLen - 1] = '\0';
}
*/