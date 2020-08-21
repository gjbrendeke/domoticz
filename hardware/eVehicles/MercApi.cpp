/************************************************************************

Merceded implementation of VehicleApi baseclass
Author: KidDigital (github.com/kiddigital)

24/07/2020 1.0 Creation

License: Public domain

************************************************************************/
#include "stdafx.h"
#include "MercApi.h"
#include "VehicleApi.h"
#include "../../main/Logger.h"
#include "../../httpclient/UrlEncode.h"
#include "../../httpclient/HTTPClient.h"
#include "../../main/json_helper.h"
#include "../../main/Helper.h"
#include "../../webserver/Base64.h"
#include <sstream>
#include <iomanip>

// based on API's as described on https://developer.mercedes-benz.com/products
// Assumes that one has a registered Mercedes ME account and using the MB developer portal
// registers for the following BYOCAR (Bring Your Own CAR) API's:
// - Fuel Status
// - Pay as you drive
// - Vehicle Lock status
// - Vehicle status
// - Electric Vehicle status (even possible for non-electric/hybrid vehicles)
// so use the following 5 scope's: mb:vehicle:mbdata:vehiclestatus mb:vehicle:mbdata:fuelstatus mb:vehicle:mbdata:payasyoudrive mb:vehicle:mbdata:vehiclelock mb:vehicle:mbdata:evstatus
#define MERC_URL_AUTH "https://api.secure.mercedes-benz.com"
#define MERC_API_AUTH "/oidc10/auth/oauth/v2/authorize"
#define MERC_API_TOKEN "/oidc10/auth/oauth/v2/token"
#define MERC_URL "https://api.mercedes-benz.com"
#define MERC_API "/vehicledata/v1/vehicles"

#define MBAPITIMEOUT (30)

CMercApi::CMercApi(const std::string username, const std::string password, const std::string vinnr)
{
	m_username = username;
	m_password = base64_encode(username);
	m_VIN = vinnr;
	m_authtoken = "";
	m_refreshtoken = password;
	m_carid = 0;
	m_config.car_name = "";
	m_config.unit_miles = false;
	m_config.distance_unit = "km";

	m_authenticating = false;
	m_crc = 0;
	m_fields = "";

	m_capabilities.has_battery_level = false;
	m_capabilities.has_charge_command = false;
	m_capabilities.has_climate_command = false;
	m_capabilities.has_defrost_command = false;
	m_capabilities.has_inside_temp = false;
	m_capabilities.has_outside_temp = false;
	m_capabilities.has_odo = true;
	m_capabilities.has_lock_status = true;
	m_capabilities.has_charge_limit = false;
	m_capabilities.has_custom_data = true;
	m_capabilities.sleep_interval = 0;
}

CMercApi::~CMercApi()
{
}

bool CMercApi::Login()
{
	_log.Log(LOG_NORM, "MercApi: Attempting login (using Refresh token for now!).");
	return RefreshLogin();
}

bool CMercApi::RefreshLogin()
{
	_log.Log(LOG_NORM, "MercApi: Refreshing login credentials.");
	m_authenticating = true;
	if (GetAuthToken("", "", true))
	{	
		_log.Log(LOG_NORM, "MercApi: Refresh successful.");
		m_authenticating = false;
		return true;
	}

	_log.Log(LOG_ERROR, "MercApi: Failed to refresh login credentials.");
	m_authtoken = "";
	m_refreshtoken = "";
	m_authenticating = false;
	return false;
}

bool CMercApi::GetAllData(tAllCarData& data)
{
	bool bSucces = false;

	bSucces = GetVehicleData(data.vehicle);
	bSucces = bSucces && GetLocationData(data.location);
	bSucces = bSucces && GetChargeData(data.charge);
	bSucces = bSucces && GetClimateData(data.climate);
	bSucces = bSucces && GetCustomData(data.custom);

	return bSucces;
}

bool CMercApi::GetLocationData(tLocationData& data)
{
	return true; // not implemented for Mercedes
	/*
	Json::Value reply;

	if (GetData("drive_state", reply))
	{
		GetLocationData(reply["response"], data);
		return true;
	}
	return false;
	*/
}

void CMercApi::GetLocationData(Json::Value& jsondata, tLocationData& data)
{
	std::string CarLatitude = jsondata["latitude"].asString();
	std::string CarLongitude = jsondata["longitude"].asString();
	data.speed = jsondata["speed"].asInt();
	data.is_driving = data.speed > 0;
	data.latitude = std::stod(CarLatitude);
	data.longitude = std::stod(CarLongitude);
}

bool CMercApi::GetChargeData(CVehicleApi::tChargeData& data)
{
	return true; // not implemented for Mercedes
	/*
	Json::Value reply;

	if (GetData("charge_state", reply))
	{
		GetChargeData(reply["response"], data);
		return true;
	}
	return false;
	*/
}

void CMercApi::GetChargeData(Json::Value& jsondata, CVehicleApi::tChargeData& data)
{
	data.battery_level = jsondata["battery_level"].asFloat();
	data.status_string = jsondata["charging_state"].asString();
	data.is_connected = (data.status_string != "Disconnected");
	data.is_charging = (data.status_string == "Charging") || (data.status_string == "Starting");
	data.charge_limit = jsondata["charge_limit_soc"].asInt();

	if(data.status_string == "Disconnected")
		data.status_string = "Charge Cable Disconnected";

	if (data.is_charging)
	{
		data.status_string.append(" (until ");
		data.status_string.append(std::to_string(data.charge_limit));
		data.status_string.append("%)");
	}
}

bool CMercApi::GetClimateData(tClimateData& data)
{
	return true; // not implemented for Mercedes
	/*
	Json::Value reply;

	if (GetData("climate_state", reply))
	{
		GetClimateData(reply["response"], data);
		return true;
	}
	return false;
	*/
}

void CMercApi::GetClimateData(Json::Value& jsondata, tClimateData& data)
{
	data.inside_temp = jsondata["inside_temp"].asFloat();
	data.outside_temp = jsondata["outside_temp"].asFloat();
	data.is_climate_on = jsondata["is_climate_on"].asBool();
	data.is_defrost_on = (jsondata["defrost_mode"].asInt() != 0);
}

bool CMercApi::GetVehicleData(tVehicleData& data)
{
	Json::Value reply;
	bool bData = false;

	if (GetData("vehiclelockstatus", reply))
	{
		if (reply.size() == 0)
		{
			bData = true;	// This occurs when the API call return a 204 (No Content). So everything is valid/ok, just no data
		}
		else
		{
			if (!reply.isArray())
			{
				_log.Log(LOG_ERROR, "MercApi: Unexpected reply from VehicleLockStatus.");
			}
			else
			{
				GetVehicleData(reply, data);
				bData = true;
			}
		}
	}

	reply.clear();

	if (GetData("payasyoudrive", reply))
	{
		if (reply.size() == 0)
		{
			bData = true;	// This occurs when the API call return a 204 (No Content). So everything is valid/ok, just no data
		}
		else
		{
			if (!reply.isArray())
			{
				_log.Log(LOG_ERROR, "MercApi: Unexpected reply from PayasyouDrive.");
			}
			else
			{
				GetVehicleData(reply, data);
				bData = true;
			}
		}
	}

	return bData;
}

bool CMercApi::GetCustomData(tCustomData& data)
{
	Json::Value reply;
	bool bCustom = true;

	if (m_capabilities.has_custom_data)
	{
		std::vector<std::string> strarray;

		StringSplit(m_fields, ",", strarray);
		for(uint8_t i=0; i<strarray.size(); i++)
		{
			reply.clear();
			if (GetResourceData(strarray[i], reply))
			{
				if(reply.size() == 0)
				{
					_log.Debug(DEBUG_NORM, "MercApi: Got empty data for resource %s", strarray[i].c_str());
				}
				else
				{
					//_log.Debug(DEBUG_NORM, "MercApi: Got data for resource %s :\n%s", strarray[i].c_str(),reply.toStyledString().c_str());

					if (!reply[strarray[i]].empty())
					{
						if (reply[strarray[i]].isMember("value"))
						{
							std::string resourceValue = reply[strarray[i]]["value"].asString();

							Json::Value customItem;
							customItem["id"] = i;
							customItem["value"] = resourceValue;
							customItem["label"] = strarray[i];

							data.customdata.append(customItem);

							_log.Debug(DEBUG_NORM, "MercApi: Got data for resource (%d) %s : %s", i, strarray[i].c_str(), resourceValue.c_str());
						}
					}
				}
			}
			else
			{
				_log.Debug(DEBUG_NORM,"MercApi: Failed to retrieve data for resource %s!", strarray[i].c_str());
			}
		}
	}

	return bCustom;
}

void CMercApi::GetVehicleData(Json::Value& jsondata, tVehicleData& data)
{
	uint8_t cnt = 0;
	do
	{
		Json::Value iter;

		iter = jsondata[cnt];
		for (auto const& id : iter.getMemberNames())
		{
			if(!(iter[id].isNull()))
			{
				_log.Debug(DEBUG_NORM, "MercApi: Found non empty field %s", id.c_str());

				if (id == "doorlockstatusvehicle")
				{
					Json::Value iter2;
					iter2 = iter[id];
					if(!iter2["value"].empty())
					{
						_log.Debug(DEBUG_NORM, "MercApi: DoorLockStatusVehicle has value %s", iter2["value"].asString().c_str());
						data.car_open = (iter2["value"].asString() == "1" || iter2["value"].asString() == "2" ? false : true);
						data.car_open_message = (data.car_open ? "Your Mercedes is open" : "Your Mercedes is locked");
					}
				}
				if (id == "odo")
				{
					Json::Value iter2;
					iter2 = iter[id];
					if(!iter2["value"].empty())
					{
						_log.Debug(DEBUG_NORM, "MercApi: Odo has value %s", iter2["value"].asString().c_str());
						data.odo = atof(iter2["value"].asString().c_str());
					}
				}
			}
		}
		cnt++;
	} while (!jsondata[cnt].empty());

	/*
	data.odo = (jsondata["odometer"].asFloat());
	if (!m_config.unit_miles)
		data.odo = data.odo * (float)1.60934;

	if (jsondata["df"].asBool() ||
		jsondata["pf"].asBool() ||
		jsondata["pr"].asBool() ||
		jsondata["dr"].asBool())
	{
		data.car_open = true;
		data.car_open_message = "Door open";
	}
	else if (
		jsondata["ft"].asBool() ||
		jsondata["rt"].asBool())
	{
		data.car_open = true;
		data.car_open_message = "Trunk open";
	}
	else if (
		jsondata["fd_window"].asBool() ||
		jsondata["fp_window"].asBool() ||
		jsondata["rp_window"].asBool() ||
		jsondata["rd_window"].asBool())
	{
		data.car_open = true;
		data.car_open_message = "Window open";
	}
	else if (
		!jsondata["locked"].asBool())
	{
		data.car_open = true;
		data.car_open_message = "Unlocked";
	}
	else
	{
		data.car_open = false;
		data.car_open_message = "Locked";
	}
	*/
}

bool CMercApi::GetData(std::string datatype, Json::Value& reply)
{
	std::stringstream ss;
	ss << MERC_URL << MERC_API << "/" << m_VIN << "/containers/" << datatype;
	std::string _sUrl = ss.str();
	std::string _sResponse;

	if (!SendToApi(Get, _sUrl, "", _sResponse, *(new std::vector<std::string>()), reply, true))
	{
		_log.Log(LOG_ERROR, "MercApi: Failed to get data %s.", datatype.c_str());
		return false;
	}

	_log.Debug(DEBUG_NORM, "MercApi: Get data %s received reply: %s", datatype.c_str(), _sResponse.c_str());

	return true;
}

bool CMercApi::GetResourceData(std::string datatype, Json::Value& reply)
{
	std::stringstream ss;
	ss << MERC_URL << MERC_API << "/" << m_VIN << "/resources/" << datatype;
	std::string _sUrl = ss.str();
	std::string _sResponse;

	if (!SendToApi(Get, _sUrl, "", _sResponse, *(new std::vector<std::string>()), reply, true, (MBAPITIMEOUT / 2)))
	{
		_log.Log(LOG_ERROR, "MercApi: Failed to get resource data %s.", datatype.c_str());
		return false;
	}

	_log.Debug(DEBUG_NORM, "MercApi: Get resource data %s received reply: %s", datatype.c_str(), _sResponse.c_str());

	return true;
}

bool CMercApi::IsAwake()
{
	// Current Mercedes Me (API) does not have an 'Awake' state
	// So we fake one, we just request all available resources that are available for the current (BYO)CAR

	std::stringstream ss;
	ss << MERC_URL << MERC_API << "/" << m_VIN << "/resources";
	std::string _sUrl = ss.str();
	std::string _sResponse;
	Json::Value _jsRoot;
	bool is_awake = false;
	int nr_retry = 0;

	while (!SendToApi(Get, _sUrl, "", _sResponse, *(new std::vector<std::string>()), _jsRoot, true, 10))
	{
		nr_retry++;
		if (nr_retry == 4)
		{
			_log.Log(LOG_ERROR, "Failed to get awake state (available resources)!");
			return false;
		}
	}

	if (!ProcessAvailableResources(_jsRoot))
	{
		_log.Log(LOG_ERROR, "Unable to process list of available resources!");
	}
	else
	{
		is_awake = true;
		_log.Debug(DEBUG_NORM, "MercApi: Awake state checked. We are awake.");
	}

	return(is_awake);
}

bool CMercApi::ProcessAvailableResources(Json::Value& jsondata)
{
	bool bProcessed = false;
	uint8_t cnt = 0;
	uint32_t crc = 0;
	std::stringstream ss;

	crc = jsondata.size();	// hm.. not easy to create something of a real crc32 of JSON content.. so for now we just compare the amount of 'keys'
	if (crc == m_crc)
	{
		_log.Debug(DEBUG_NORM, "CRC32 of content is the same.. skipping processing");
		return true;
	}
	else
	{
		_log.Debug(DEBUG_NORM, "CRC32 of content is the not the same (%d).. start processing", crc);
	}

	m_crc = crc;

	try
	{
		do
		{
			Json::Value iter;

			iter = jsondata[cnt];
			for (auto const& id : iter.getMemberNames())
			{
				if(!(iter[id].isNull()))
				{
					Json::Value iter2;
					iter2 = iter[id];
					//_log.Debug(DEBUG_NORM, "MercApi: Field (%d) %s has value %s",cnt, id.c_str(), iter2.asString().c_str());
					if (id == "name")
					{
						if (cnt > 0)
						{
							ss << ",";
						}
						ss << iter2.asString();
					}
					if (id == "version" && iter2.asString() != "1.0")
					{
						_log.Log(LOG_STATUS, "Found resources with another version (%s) than expected 1.0! Continueing but results may be wrong!", iter2.asString().c_str());
					}
				}
			}
			cnt++;
		} while (!jsondata[cnt].empty());

		if (ss.str().length() > 0)
		{
			m_fields = ss.str();
			_log.Log(LOG_STATUS, "Found resource fields: %s", m_fields.c_str());

			bProcessed = true;
		}
		else
		{
			_log.Debug(DEBUG_NORM, "MercApi: Found %d resource fields but none called name!",cnt);
		}
	}
	catch(const std::exception& e)
	{
		std::string what = e.what();
		_log.Log(LOG_ERROR, "MercApi: Crashed during processing of resources: %s", what.c_str());
	}

	return bProcessed;
}

bool CMercApi::SendCommand(eCommandType command, std::string parameter)
{
	std::string command_string;
	Json::Value reply;
	std::string parameters = "";

	switch (command)
	{
	case Charge_Start:
		command_string = "charge_start";
		break;
	case Charge_Stop:
		command_string = "charge_stop";
		break;
	case Climate_Off:
		command_string = "auto_conditioning_stop";
		break;
	case Climate_On:
		command_string = "auto_conditioning_start";
		break;
	case Climate_Defrost:
		command_string = "set_preconditioning_max";
		parameters = "on=true";
		break;
	case Climate_Defrost_Off:
		command_string = "set_preconditioning_max";
		parameters = "on=false";
		break;
	case Wake_Up:
		command_string = "wake_up";
		break;
	case Set_Charge_Limit:
		if (parameter == "0")
			command_string = "charge_standard";
		else if(parameter == "100")
			command_string = "charge_max_range";
		else
		{
			command_string = "set_charge_limit";
			parameters = "percent=" + parameter;
		}
		break;
	}

	if (SendCommand(command_string, reply, parameters))
	{
		if (command == Wake_Up)
		{
			if (reply["response"]["state"].asString() == "online")
				return true;
		}
		else
		{
			if (reply["response"]["result"].asString() == "true")
				return true;
		}
	}

	return false;
}

bool CMercApi::SendCommand(std::string command, Json::Value& reply, std::string parameters)
{
	/*
	std::stringstream ss;
	if (command == "wake_up")
		ss << TESLA_URL << TESLA_API << "/" << m_carid << "/" << command;
	else
		ss << TESLA_URL << TESLA_API << "/" << m_carid << TESLA_API_COMMAND << command;

	std::string _sUrl = ss.str();
	std::string _sResponse;

	std::stringstream parss;
	parss << parameters;
	std::string sPostData = parss.str();

	if (!SendToApi(Post, _sUrl, sPostData, _sResponse, *(new std::vector<std::string>()), reply, true))
	{
		_log.Log(LOG_ERROR, "MercApi: Failed to send command %s.", command.c_str());
		return false;
	}

	//_log.Log(LOG_NORM, "MercApi: Command %s received reply: %s", command.c_str(), _sResponse.c_str());
	_log.Debug(DEBUG_NORM, "MercApi: Command %s received reply: %s", command.c_str(), _sResponse.c_str());
	*/
	return true;
}

// Requests an access token from the MB OAuth Api.
bool CMercApi::GetAuthToken(const std::string username, const std::string password, const bool refreshUsingToken)
{
	/*
	if (username.size() == 0 && !refreshUsingToken)
	{
		_log.Log(LOG_ERROR, "MercApi: No username specified.");
		return false;
	}
	if (username.size() == 0 && !refreshUsingToken)
	{
		_log.Log(LOG_ERROR, "MercApi: No password specified.");
		return false;
	}
	*/
	if (refreshUsingToken && m_refreshtoken.size() == 0)
	{
		_log.Log(LOG_ERROR, "MercApi: No refresh token to perform refresh!");
		return false;
	}

	std::stringstream ss;
	ss << MERC_URL_AUTH << MERC_API_TOKEN;
	std::string _sUrl = ss.str();
	std::ostringstream s;
	std::string _sGrantType = (refreshUsingToken ? "refresh_token" : "code");

	s << "grant_type=" << _sGrantType;

	if (refreshUsingToken)
	{
		s << "&refresh_token=" << m_refreshtoken;
	}
	else
	{
		_log.Log(LOG_ERROR, "MercApi: Failed to get token. Only Refresh supported for now!");
		return false;
	}

	std::string sPostData = s.str();

	std::vector<std::string> _vExtraHeaders;
	_vExtraHeaders.push_back("Content-Type: application/x-www-form-urlencoded");
	_vExtraHeaders.push_back("Authorization: Basic " + m_password);

	std::string _sResponse;
	Json::Value _jsRoot;

	if (!SendToApi(Post, _sUrl, sPostData, _sResponse, _vExtraHeaders, _jsRoot, false))
	{
		_log.Log(LOG_ERROR, "MercApi: Failed to get token.");
		return false;
	}

	if(!_jsRoot["error"].empty()) 
	{
		_log.Log(LOG_ERROR, "MercApi: Received error response (%s).", _jsRoot["error"].asString().c_str());
		return false;
	}

	m_authtoken = _jsRoot["access_token"].asString();
	if (m_authtoken.size() == 0)
	{
		_log.Log(LOG_ERROR, "MercApi: Received token is zero length.");
		return false;
	}

	m_refreshtoken = _jsRoot["refresh_token"].asString();
	if (m_refreshtoken.size() == 0)
	{
		_log.Log(LOG_ERROR, "MercApi: Received refresh token is zero length.");
		return false;
	}
	else
	{
		_log.Log(LOG_STATUS, "MercApi: Received new refresh token %s .", m_refreshtoken.c_str());
	}
	

	_log.Debug(DEBUG_NORM, "MercApi: Received access token from API.");

	return true;
}

// Sends a request to the MB API.
bool CMercApi::SendToApi(const eApiMethod eMethod, const std::string& sUrl, const std::string& sPostData,
	std::string& sResponse, const std::vector<std::string>& vExtraHeaders, Json::Value& jsDecodedResponse, const bool bSendAuthHeaders, const int timeout)
{
	try 
	{
		// If there is no token stored then there is no point in doing a request. Unless we specifically
		// decide not to do authentication.
		if (m_authtoken.size() == 0 && bSendAuthHeaders) 
		{
			_log.Log(LOG_ERROR, "MercApi: No access token available.");
			return false;
		}

		// Prepare the headers. Copy supplied vector.
		std::vector<std::string> _vExtraHeaders = vExtraHeaders;

		// If the supplied postdata validates as json, add an appropriate content type header
		if (sPostData.size() > 0)
			if (ParseJSon(sPostData, *(new Json::Value))) 
				_vExtraHeaders.push_back("Content-Type: application/json");

		// Prepare the authentication headers if requested.
		if (bSendAuthHeaders) 
			_vExtraHeaders.push_back("Authorization: Bearer " + m_authtoken);

		// Increase default timeout
		if(timeout == 0)
		{
			HTTPClient::SetConnectionTimeout(MBAPITIMEOUT);
			HTTPClient::SetTimeout(MBAPITIMEOUT);
		}
		else
		{
			HTTPClient::SetConnectionTimeout(timeout);
			HTTPClient::SetTimeout(timeout);
		}

		std::vector<std::string> _vResponseHeaders;
		std::stringstream _ssResponseHeaderString;
		uint16_t _iHttpCode;

		_log.Debug(DEBUG_RECEIVED, "MercApi: Performing request to Api: %s", sUrl.c_str());

		switch (eMethod)
		{
		case Post:
			if (!HTTPClient::POST(sUrl, sPostData, _vExtraHeaders, sResponse, _vResponseHeaders))
			{
				_iHttpCode = (!_vResponseHeaders[0].empty() ? (uint16_t) std::stoi(_vResponseHeaders[0].substr(9,3).c_str()) : 9999);
				_log.Log(LOG_ERROR, "Failed to perform POST request (%d)!", _iHttpCode);
			}
			break;

		case Get:
			if (!HTTPClient::GET(sUrl, _vExtraHeaders, sResponse, _vResponseHeaders, true))
			{
				_iHttpCode = (!_vResponseHeaders[0].empty() ? (uint16_t) std::stoi(_vResponseHeaders[0].substr(9,3).c_str()) : 9999);
				_log.Log(LOG_ERROR, "Failed to perform GET request (%d)!", _iHttpCode);
			}
			break;

		default:
			{
				_log.Log(LOG_ERROR, "MercApi: Unknown method specified.");
				return false;
			}
		}

		_iHttpCode = (!_vResponseHeaders[0].empty() ? (uint16_t) std::stoi(_vResponseHeaders[0].substr(9,3).c_str()) : 0);

		// Debug response
		for (unsigned int i = 0; i < _vResponseHeaders.size(); i++) 
			_ssResponseHeaderString << _vResponseHeaders[i];
		_log.Debug(DEBUG_RECEIVED, "MercApi: Performed request to Api: (%d)\n%s\nResponse headers: %s", _iHttpCode, sResponse.c_str(), _ssResponseHeaderString.str().c_str());

		switch(_iHttpCode)
		{
		case 200:
			break; // Ok, continue to process content
		case 204:
			_log.Log(LOG_STATUS, "Received (204) No Content.. likely because of no activity/updates in the last 12 hours!");
			return true; // OK and directly return as there is no content to actually process
			break;
		case 400:
		case 401:
			if(!m_authenticating) 
			{
				_log.Log(LOG_STATUS, "Received 400/401.. Let's try to (re)authorize again!");
				RefreshLogin();
			}
			else
			{
				_log.Log(LOG_STATUS, "Received 400/401.. During authorisation proces. Aborting!");
			}			
			return false;
			break;
		case 429:
			_log.Log(LOG_STATUS, "Received 429.. Too many request... we need to back off!");
			return false;
			break;
		case 500:
		case 503:
			_log.Log(LOG_STATUS, "Received 500/503.. Service is not available!");
			return false;
			break;
		default:
			_log.Log(LOG_STATUS, "Received unhandled HTTP returncode %d !", _iHttpCode);
			return false;
		}

		if (sResponse.size() == 0)
		{
			_log.Log(LOG_ERROR, "MercApi: Received an empty response from Api (HTTP %d).", _iHttpCode);
			return false;
		}

		if (!ParseJSon(sResponse, jsDecodedResponse))
		{
			_log.Log(LOG_ERROR, "MercApi: Failed to decode Json response from Api (HTTP %d).", _iHttpCode);
			return false;
		}
	}
	catch (std::exception & e)
	{
		std::string what = e.what();
		_log.Log(LOG_ERROR, "MercApi: Error sending information to Api: %s", what.c_str());
		return false;
	}
	return true;
}
