#include <ESP8266WiFi.h>
#include "ESPAsyncWebServer.h"
#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>
#include "FS.h"
#include <ESP8266mDNS.h>
#include <Wire.h>

#define SLAVE_RESET_PIN 2

const char* ssid = "MITRA_TAX";
const char* password = "Mitra@1667";
const char* names[2];

const char* version = "v1.0";

bool shouldReboot = false;
bool shouldAssembleUI = true;
bool configExists = true;
bool haveClient = false;

long oldTime = 0;

int GPIO[25][2];		//Stores which GPIO is in which mode
int elementIndex = 0;	//Stores the no of elements for the UI

typedef struct elementType {	//Arduino equivalent of Json for the UI
	unsigned int type;
	unsigned int id;
	String label;
	String value;
	unsigned int color;
} elementType;
elementType *elementList[25];

//Initialize the server
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncWebSocketClient *connectedClient;

void sendUpdate(String msg, int i){		//send Updates to the connected client
	Serial.println("Assigning values to json");
	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& changeElement = jsonBuffer.createObject();
	changeElement["type"]=6;
	changeElement["value"]=msg;
	changeElement["id"]=i;
	Serial.println("Forwarding data");
	forwardText(changeElement);
}

void checkStates(){	//read all the input pin states
	int val;
	String msg;
	for(int i=0;i<elementIndex;i++){
		if(GPIO[i][1]==2){
			//Serial.println("Reading digital value");
			val = digitalRead(GPIO[i][0]);
			if(val==0){
				msg="OFF";
			}
			else
				msg="ON";
			if(haveClient)
				sendUpdate(msg,i);
		}
		else if(GPIO[i][1]==4){
			//Serial.println("Reading analog value");
			val = analogRead(A0);
			val = map(val,0,1024,0,100);
			msg = String(val);
			msg += "%";
			if(haveClient)
				sendUpdate(msg,i);
		}
		else if(GPIO[i][1]==5){
			Wire.requestFrom(GPIO[i][0],1);
			val=-1000;
			while(Wire.available()){
				val = Wire.read();
				if(val>254){
					val=val-255;
					val*=-1;
				}
			}
			msg = String(val);
			if(haveClient)
				sendUpdate(msg,i);
		}
		else{
			//Serial.println("nothing to check");
		}
	}
}

void decodeMsg(String msg){	//decode the incomming msg from a connected client
	int id = msg.substring(msg.lastIndexOf(':') + 1).toInt();	//get the ID of updated element
	//Serial.print("msg received from id : ");
	//Serial.println(id);
	int val;
	if(msg.startsWith("sactive:")){	//act on the imcomming msg
		digitalWrite(GPIO[id][0],HIGH);
		val = 1;
	}
	else if(msg.startsWith("sinactive:")){
		digitalWrite(GPIO[id][0],LOW);
		val = 0;
	}
	else if(msg.startsWith("slvalue:")){
		val = msg.substring(msg.indexOf(':')+1,msg.lastIndexOf(':')).toInt();
		val=map(val,0,100,0,255);
		analogWrite(GPIO[id][0],val);
	}
}

void createElement(int type,String label,String value,int color,int id){	//stores the property of each UI element in the elementList array
	//Serial.println("Creating UI Element");
	elementType *newElement = new elementType();
	//Serial.println("Storing values");
	//Serial.print("Value of element Index is ");
	//Serial.println(elementIndex);
	newElement->type=type;
	newElement->id=id;
	newElement->label=label;
	newElement->value=value;
	newElement->color=color;
	elementList[elementIndex]=newElement;
	elementIndex++;
}

void setupUI(){	//reads the configuration file and decodes the data to create individual UI elements
	File configFile = SPIFFS.open("/config.json", "r");
	if (!configFile) {
		Serial.println("You haven't configured the device yet!");
		configExists=false;
		return;
	}

	size_t size = configFile.size();
	if (size > 1024) {
		Serial.println("Config file size is too large");
		return;
	}

	configExists=true;

	// Read The Config File
	std::unique_ptr<char[]> buf(new char[size]);
	configFile.readBytes(buf.get(), size);
	configFile.close();

	//Store the file as json object
	StaticJsonBuffer<1000> jsonBuffer;
	JsonObject& configFileJson = jsonBuffer.parseObject(buf.get());
	if(!configFileJson.success()){
		Serial.println("Json Parse Failed!");
		return;
	}
	//Serial.println("Parse Success!");

	int elementNo = configFileJson["pin"].size();
	Serial.print("Number of Elements: ");
	Serial.println(elementNo);

	for(int i=0;i<elementNo;i++){
		int temp,temp2;
		const char* text;
		String text2;
		/*
		Serial.print("Connection ");
		Serial.print(i);
		Serial.print(" is of type ");
		Serial.println(temp);
		*/

		//pin no and their connection types are stored in the GPIO array
		temp = configFileJson["connection"][i];
		temp2 = configFileJson["pin"][i];
		GPIO[i][0]=temp2;	//pin NO
		GPIO[i][1]=temp;	//connection type

		int type = configFileJson["connection"][i];
		String value;
		//Select by connection types
		switch(type){
			case 1:	//DigitalOUT
				//Serial.println("Running case 1");
				temp = configFileJson["pin"][i];
				//Serial.println("Setting pinMode");
				pinMode(temp,OUTPUT);	//set pinMode as OUTPUT
				temp2 = configFileJson["value"][i];
				if(temp2!=0&&temp2!=1){			//check if a previous value is stored
					digitalWrite(temp,LOW);	//if not, then set the pin low
					configFileJson["value"][i]=0;
				}
				else{
					temp2=configFileJson["value"][i];
					digitalWrite(temp,temp2);
				}
				if(temp2==0)
					value="false";
				else
					value="true";
				//Create a switch
				text=configFileJson["name"][i];
				//Serial.println(text);
				createElement(3,text,value,0,i);
				break;

			case 2: //Digital Input
				//Serial.println("Running case 2");
				temp=configFileJson["pin"][i];
				pinMode(temp,INPUT);
				temp2==digitalRead(temp);
				configFileJson["value"][i]=temp2;
				//Create a label
				temp2=configFileJson["value"][i];
				if(temp2==HIGH){
					value="ON";
				}
				else
					value="OFF";
				createElement(1,configFileJson["name"][i],value,1,i);
				break;

			case 3:	//analog OUTPUT
				//Serial.println("Running case 3");
				temp=configFileJson["pin"][i];
				pinMode(temp,OUTPUT);
				temp2=configFileJson["value"][i];
				if(!(temp2>-1&&temp2<101)){
					analogWrite(temp,0);
					configFileJson["value"][i]=0;
				}
				else{
					analogWrite(temp,map(temp2,0,100,0,255));
				}
				temp = configFileJson["value"][i];
				value = String(temp);
				//Create a slider
				createElement(8,configFileJson["name"][i],value,2,i);
				break;

			case 4:	//analog INPUT
				//Serial.println("Running case 4");
				temp=configFileJson["pin"][i];
				temp2=analogRead(A0);	//regardless of the pin selected only A0 is set as Analog IN
				configFileJson["value"][i]=temp2;
				temp = configFileJson["value"][i];
				value = String(temp);
				//Create a label
				createElement(1,configFileJson["name"][i],value,3,i);
				break;
				
			case 5:	//I2C INPUT
				temp=configFileJson["pin"][i];
				if(configFileJson["name"][i]=="Temperature")
					GPIO[i][0]=38;
				Wire.requestFrom(temp,1);
				temp2=-1000;
				while(Wire.available()){
					temp2 = Wire.read();
				}
				if(temp2>254){
					temp2=temp2-255;
					temp2*=-1;
				}
				configFileJson["value"][i]=temp2;
				value = String(temp2);
				createElement(1,configFileJson["name"][i],value,4,i);
				break;
		}
	}
}

void checkUpdate(){	//check the /update.json file to see if a update is availaible
	File updateFile = SPIFFS.open("/update.json", "r");
	if (!updateFile) {
		Serial.println("No update file exists");
		return;
	}

	size_t size = updateFile.size();
	if (size > 1024) {
		Serial.println("Update file size is too large");
		return;
	}

	// Read The Update File
	std::unique_ptr<char[]> buf(new char[size]);
	updateFile.readBytes(buf.get(), size);
	updateFile.close();

	//Store the file as json object
	StaticJsonBuffer<200> jsonBuffer;
	JsonObject& updateJson = jsonBuffer.parseObject(buf.get());
	if(!updateJson.success()){
		Serial.println("Json Parse Failed!");
		return;
	}
	//Serial.println("Parse Success!");

	int allowUpdate = updateJson["allow"];
	const char* url = updateJson["URL"];
	if(allowUpdate==0){
		Serial.printf("No update for now.");
		return;
	}

	JsonObject& newJson = jsonBuffer.createObject();

	//change the Update file to indicate not to update again
	newJson["allow"]=0;
	newJson["URL"]="0.0.0.0";

	SPIFFS.remove("/update.json");
	File newUpdateFile = SPIFFS.open("/update.json", "w");
	if (!newUpdateFile) {
		Serial.println("Error oppening the update file");
		return;
	}

	size = newUpdateFile.size();
	if (size > 1024) {
		Serial.println("Update file size is too large");
		return;
	}

	newJson.printTo(newUpdateFile);
	//Serial.println("update file updated");

	Serial.println("Update Started!");
	ESPhttpUpdate.update(url, 80, "/update/update.bin");
	Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
}

//WebSocket Event Handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
	switch(type){
		case WS_EVT_DISCONNECT:
			Serial.println("Disconnected!");
			haveClient=false;	//indicate that there are no Clients connected.
			break;
		case WS_EVT_CONNECT:
			Serial.print("Connected: ");
			Serial.println(client->id());
			sendUI(client);		//if a client is connected then transmit the UI json file
			break;
		case WS_EVT_DATA:
			String msg = "";
			for(int i=0;i<len;i++)
				msg += (char)data[i];
			Serial.println(msg);
			decodeMsg(msg);		//decode any received msg
			break;
	}
}

//Send UI json
void sendUI(AsyncWebSocketClient *client){
	connectedClient=client;
	haveClient = true;
	if(!configExists){	//if configurationFile doesn't exist
		String data;
		StaticJsonBuffer<200> jsonBuffer;
		JsonObject &root = jsonBuffer.createObject();
		root["type"]=0;
		root["label"]="You Haven't Configured Anything Yet!";
		root.printTo(data);
		root.printTo(Serial);
		client->text(data);
		return;
	}
	for(int i=-1;i<elementIndex;i++){	//esemble json from the elementList array.
		String data;
		StaticJsonBuffer<1000> jsonBuffer;
		JsonObject &root = jsonBuffer.createObject();
		if(i==-1){
			root["type"]=0;
			root["label"]="LiveSparks";
		}
		else{
			root["type"]=elementList[i]->type;
			root["label"]=elementList[i]->label;
			if(elementList[i]->value=="false")
				root["value"]=false;
			else if(elementList[i]->value=="true")
				root["value"]=true;
			else
				root["value"]=elementList[i]->value;
			root["color"]=String(elementList[i]->color);
			root["id"]=String(elementList[i]->id);
		}
		root.printTo(data);
		root.printTo(Serial);
		Serial.println("");
		client->text(data);	//text the data to the client
	}
}

void forwardText(JsonObject& body){	//send this Json to the latest connected client
	String data = "";
	body.printTo(data);
	body.printTo(Serial);
	Serial.println("");
	if(!haveClient){
		Serial.println("No Client Connected");
		return;
	}
	connectedClient->text(data);
}

void setup() //MAIN SETUP
{
	SPIFFS.begin();
	Serial.begin(115200);
	
	pinMode(SLAVE_RESET_PIN,OUTPUT);
	Wire.begin();
	digitalWrite(SLAVE_RESET_PIN, LOW);
  	delay(10);
  	digitalWrite(SLAVE_RESET_PIN, HIGH);

	// Connect to WiFi network
	WiFi.hostname("LiveSparks");
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	Serial.println("");
	Serial.println(version);

	// Wait for connection
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.print("Connected to ");
	Serial.println(ssid);
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

	//Check if the device was rebooted for an Update.
	checkUpdate();

	//WebSocket Handler
	ws.onEvent(onWsEvent);
	server.addHandler(&ws);

	//Serve the index.htm file at adress "/"
	server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

	server.serveStatic("/config", SPIFFS, "/configureUI.html");

	//Handle POST requests containig data
	server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
		
		/*~~~~~~~~~~~~HANDLE UPDATE REQUESTS~~~~~~~~~~~~~~~~*/
		if(request->url()=="/update"){
			StaticJsonBuffer<200> jsonBuffer;
			JsonObject& body = jsonBuffer.parseObject((const char*)data);
			if(!body.success()){
				request->send(200,"text/plain","Json Parse Failed");
				Serial.println("Json Parse Failed.");
				return;
			}
			const char* url = body["URL"];
			Serial.println(url);
			request->send(200,"text/plain","Got IT!");
			SPIFFS.remove("/update.json");	//delete the prev update file
			//write the recieved data to a new update file
			File updateFile = SPIFFS.open("/update.json", "w");
			if (!updateFile) {
				Serial.println("Error oppening the update file");
				return;
			}
			size_t size = updateFile.size();
			if (size > 1024) {
				Serial.println("Update file size is too large");
				return;
			}
			body.printTo(updateFile);
			Serial.println("Update Request Received");
			shouldReboot=true; //indicate that the system needs to reboot
		}
		
		/*~~~~~~~~~~~~HANDLE CONFIGURATION REQUESTS~~~~~~~~~~~~~~~~*/
		if(request->url()=="/config"){
			StaticJsonBuffer<1000> jsonBuffer;
			JsonObject& body =jsonBuffer.parseObject((const char*)data);
			if(!body.success()){
				request->send(200,"text/plain","Parse Fail!");
				Serial.println("Json Parse Failed!");
				return;
			}
			request->send(200,"text/plain","Got IT!");
			//Serial.printf("Parse Successful!");
			File configFile = SPIFFS.open("/config.json", "w");
			if (!configFile) {
				Serial.println("Failed to open config file for writing");
			}
			else{
				body.printTo(configFile);
				configFile.close();
				elementIndex=0;
				shouldAssembleUI=true;	//indicate that a new UI needs to be assembled
			}
		}

		/*~~~~~~~~~~~~HANDLE FORWARD REQUESTS~~~~~~~~~~~~~~~~*/
		if(request->url()=="/forward"){	//data received here is forwarded to the client
			StaticJsonBuffer<1000> jsonBuffer;
			JsonObject& body = jsonBuffer.parseObject((const char*)data);
			if(!body.success()){
				request->send(200,"text/plain","Parse Fail!");
				Serial.println("Json Parse Failed!");
				return;
			}
			request->send(200,"text/plain","Got IT!");
			Serial.printf("Parse Successful!");
			forwardText(body);
		}
	});
	//Start the async server
	server.begin();
}

void loop()
{
	if(shouldReboot){
		Serial.println("Rebooting...");
		delay(100);
		ESP.restart();
	}

	if(shouldAssembleUI){
		setupUI();
		shouldAssembleUI=false;
	}

	if((millis()-oldTime)>500){ //chech the input pin states every 500ms
		checkStates();
		oldTime=millis();/*
		int size = Wire.requestFrom(I2C_SLAVE_ADDR,1);
		int val = -1000;

		if(Wire.available())
			val = Wire.read();

		Serial.print("Recieved Size: ");
		Serial.print(size);
		Serial.print(", Received value: ");
		Serial.println(val);
		String msg = (String)val;
		if(haveClient)
			sendUpdate(msg,0);*/
	}
	
}