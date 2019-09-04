import java.io.*;
import org.apache.commons.io.FileUtils;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;

import com.alibaba.fastjson.JSON;
import com.alibaba.fastjson.JSONObject;
import com.alibaba.fastjson.JSONArray;


public class Main{

	public interface Clibrary extends Library {

		Clibrary INSTANCE = (Clibrary)Native.loadLibrary("secstore", Clibrary.class);

		public int SEC_storage_api_init(PointerByReference pbr, String str);
		public int SEC_storage_api_free(Pointer p);
        public int SEC_storage_api_store_json(Pointer p, String invoiceId, String jsonStr, 
										String globalID, String parentID, PointerByReference reIDPbr);
        public int SEC_storage_api_store_pdf(Pointer p, String invoiceId, byte[] fileBytes, 
										int fileLen, String globalID, String parentID, PointerByReference reIDPbr);
        public int SEC_storage_api_store_img(Pointer p, String invoiceId, byte[] fileBytes, 
										int fileLen, String globalID, String parentID, PointerByReference reIDPbr);
        public int SEC_storage_api_store_ofd(Pointer p, String invoiceId, byte[] fileBytes, 
										int fileLen, String globalID, String parentID, PointerByReference reIDPbr);
        public int SEC_storage_api_update_status(Pointer p, String invoiceId, String jsonStr, 
										String globalID, String parentID, PointerByReference reIDPbr);
        public int SEC_storage_api_update_ownership(Pointer p, String invoiceId, String jsonStr, 
										String globalID, String parentID, PointerByReference reIDPbr);
        public int SEC_storage_api_update_reim(Pointer p, String invoiceId, String jsonStr, 
										String globalID, String parentID, PointerByReference reIDPbr);
	}

	public static void main(String[] args) throws Exception{

		PointerByReference pbr = new PointerByReference();
		Clibrary.INSTANCE.SEC_storage_api_init(pbr, "./gateway.ini");
		Pointer p = pbr.getValue();

		//String invoiceId = "12345678123456781234567812345678";
		String invoiceId = "";
		String globalID = "22222222222222222";
		String parentID = "123456";
		PointerByReference reIDPbr = new PointerByReference();



		int i = 1;
		while(i <= 1000) {
			invoiceId += i;

		File file = new File("1.json");
		String content = FileUtils.readFileToString(file);
		JSONObject json = JSON.parseObject(content);
//		System.out.println(JSON.toJSONString(json, false));

		byte[] fileBytes;
		File pdfFile = new File("1.pdf");
		FileInputStream fis = new FileInputStream(pdfFile);
		ByteArrayOutputStream baos = new ByteArrayOutputStream();
		byte[] buff = new byte[1024];
		int len;
		while((len = fis.read(buff)) != -1) {
			baos.write(buff, 0, len);
		}
		fileBytes = baos.toByteArray();
		System.out.println("---> " + fileBytes.length);

        if(Clibrary.INSTANCE.SEC_storage_api_store_json(p, invoiceId, JSON.toJSONString(json, false), globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_store_pdf(p, invoiceId, fileBytes, fileBytes.length, globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_store_img(p, invoiceId, fileBytes, fileBytes.length, globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_store_ofd(p, invoiceId, fileBytes, fileBytes.length, globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_update_status(p, invoiceId, "{\"accStatus\":\"X\", \"revStatus\":\"Y\"}", globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_update_ownership(p, invoiceId, "{\"invOwnNum\":\"66666\"}", globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_update_reim(p, invoiceId, "{\"reimburseNum\":\"99999\"}", globalID, parentID, reIDPbr) == 0) {
        //if(Clibrary.INSTANCE.SEC_storage_api_update_reim(p, invoiceId, "{\"reimburseIssuDate\":\"20190202\"}", globalID, parentID, reIDPbr) == 0) {
            System.out.println("SUCCEED");
        } else {
            System.out.println("FAIL");
        }
			invoiceId = "";
			i++;
			Pointer reIDP = reIDPbr.getValue();
			String reID = reIDP.getString(0);
			System.out.println("reID is  " + reID);
		}

		Clibrary.INSTANCE.SEC_storage_api_free(p);
	}
}
