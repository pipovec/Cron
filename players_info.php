<?php 

/*
* Docasne ziskavanie players_info, kym neopravim C++, ktore zerie vela pamati az 
* sa zatavi na nedostatok
*
*/
declare(strict_types=1);

$ins_counter = 0;

class json3 {
    
    private $server         = "https://api.worldoftanks.eu/wot";
    private $server2        = "https://api.worldoftanks.eu/wgn";
    private $api_id         = 'c428e2923f3d626de8cbcb3938bb68f8';
    private $language       = 'cs';
        private function SendRequest($method, $fields_string)
        {
            $server = $this->server.$method;
            $sleep = 0;
            
            $ch = curl_init();
            curl_setopt($ch,CURLOPT_URL,  $server);
            curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
            curl_setopt($ch,CURLOPT_POSTFIELDS,$fields_string);
            do{
            sleep($sleep);    
            $result = curl_exec($ch);
            if($result === false)
                {echo 'Curl error: ' . curl_error($ch)." cas je nastaveny na ".$sleep." \n";}
            $sleep = $sleep + 2;
            if($sleep > 20){die('Vyplo sa to po casovom limite na SendRequest');}
            }
            while($result === false);
            
                
            curl_close($ch);
            return $result;
        }
        
        
        /* Poslanie CURL cez http://api.worldoftanks.eu/wgn */
        private function SendRequest2($method, $fields_string)
        {
            
            $server = $this->server2.$method;
            $sleep = 0;
            
            
            
            $ch = curl_init();
            curl_setopt($ch,CURLOPT_URL,  $server);
            curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
            curl_setopt($ch,CURLOPT_POSTFIELDS,$fields_string);
            $result = curl_exec($ch);
            curl_close($ch);
            return $result;
        }
        
        private function BasicFields()
        {
            $fields = array(
                        'application_id'=>  $this->api_id,
                        'language'=>        $this->language,
                         );
            return $fields;
        }
        private function Fields_string($fields)
        {
            $fields_string = null;
            $basic = $this->BasicFields();
            foreach($fields as $k=>$v){$basic[$k] = $v;}
            
            
            foreach($basic as $key=>$value){ $fields_string .= $key.'='.$value.'&'; }
        
            $fields_string = rtrim($fields_string,'&');
            
            
            
        return $fields_string;    
        }
        
        function Send($method, $fields){
            
            $fields_string = $this->Fields_string($fields);
            $k = 0;
            
            do{
                $result = $this->SendRequest($method, $fields_string);
                
                $json = json_decode($result, true);
                $status = $json['status'];
                
                
                switch($status){
                    case "ok": 
                        $k = 1; break;
                    case "error":
                        $k = 0; echo 'Chyba: '.$json['error']['message'].' field: '.$json['error']['field']."\n";break;
                }
            }
            while($k === 0);
                
            return $result;    
                
            }
        
        function Send2($method, $fields){
            
            $fields_string = $this->Fields_string($fields);
            $k = 0;
            
            do{
                $result = $this->SendRequest2($method, $fields_string);
                
                $json = json_decode($result, true);
                $status = $json['status'];
                
                
                switch($status){
                    case "ok": 
                        $k = 1; break;
                    case "error":
                        $k = 0; echo 'Chyba: '.$json['error']['message'].' field: '.$json['error']['field']."\n";break;
                }
            }
            while($k === 0);
                
            return $result;    
                
            }    
            
        function GetJson($method, $fields){
            
            $fields_string = $this->Fields_string($fields);
            $k = 0;
            $sleep = 0;
            
            do{
                sleep($sleep);
                $result = $this->SendRequest($method, $fields_string);
                
                if($sleep > 60){die('Vyplo sa to po casovom limite na GetJson');}
                $json = json_decode($result, true);
                
                $status = $json['status'];
                if($status != 'ok')
                {echo "Chyba json: ".$json['error']['message']." pole ".$json['error']['field']."\n";}
                $sleep = $sleep + 2;
                switch($status){
                    case "ok": 
                        $k = 1; break;
                    case "error":
                        $k = 0; break;
                }
            }
            while($k === 0);
                
            return $json; 
        }    
        
    }


class account2 extends json3{
    
    function players_info($account_id)
    {
       $method = "/account/info/"; 
       $fields = "client_language,global_rating,created_at,logout_at,last_battle_time"; 
       $fields = array('fields'=>$fields, 'account_id'=>$account_id);
       
       $data = parent::GetJson($method, $fields);
              
       return $data;
    }   
    
}

class Database {

    const DSN = "pgsql:dbname=clan;user=deamon;password=sedemtri";    
    protected $conn;
    protected $stmCheckNewData;
    protected $stmUpdateData;
    public $upd_counter = 0;
    public $ins_counter = 0;
   
    function __construct () {
        $this->Connection();         
        $this->PrepareStm();
        $this->PrepareUpd();
    }

    private function Connection () {
        try {
            $this->conn = new PDO(self::DSN);

        } catch (PDOException $e) {
            print "Chyba pripojenia k databaze: ". $e->getMessage(). "\n";
            die();
        }
    }

    private function PrepareStm() {
        $sql = 'SELECT global_rating,client_language,logout_at as la,last_battle_time as lbt FROM players_info WHERE account_id = :id';
        $this->stmCheckNewData = $this->conn->prepare($sql);
    }

    private function PrepareUpd() {
        $sql = 'UPDATE players_info SET global_rating = :gr, client_language = :cl, last_battle_time = :lbt, logout_at = :la WHERE account_id = :id';
        $this->stmUpdateData = $this->conn->prepare($sql);
    }

    public function SelectAccountId():array {
        $sql = 'SELECT account_id FROM players_all ORDER BY account_id DESC';

       $sth = $this->conn->prepare($sql);
       $sth->execute();
       $db = $sth->fetchAll();

       return $db;
    } 

    public function ChceckNewData($data) {
        $data = $data['data'];
        $update = FALSE;


        foreach($data as $id => $row) {                         

            $this->stmCheckNewData->execute(array(':id' => $id));
            $res = $this->stmCheckNewData->fetch();

            if($res) {
                    // Porovnaj jazyk
                $i = strcmp($res['client_language'],$row['client_language'] );
                if($i != 0) {
                    // Zmena jazyka - update
                    $update = TRUE;
                }
                // Porovnaj global_rating
                if($res['global_rating'] != $row['global_rating']) {
                    // Zmena global rating - update
                    $update = TRUE;
                }

                // Porovnaj last battle time
                if(strtotime($res['lbt']) != $row['last_battle_time']) {
                    // Zmena last battle time - update
                    $update = TRUE;
                }

                // Porovnah logout at
                if(strtotime($res['la']) != $row['logout_at']) {
                    // Zmena logout at - update
                    $update = TRUE;
                }
            }
            else {
                // Pridaj zaznam
                $this->conn->query("INSERT INTO players_info (account_id,global_rating,client_language,logout_at,last_battle_time,created_at) VALUES (".$id.",".$row['global_rating'].",'".$row['client_language']."','".date('Y-m-d H:i:s',$row['logout_at'])."','".date('Y-m-d H:i:s',$row['last_battle_time'])."', '".date('Y-m-d H:i:s',$row['created_at'])."')");
                $this->ins_counter ++;
            }
            
            if($update) {
                $uppp =  array(':gr' => $row['global_rating'], ':cl' => $row['client_language'], ':lbt'=> date('Y-m-d H:i:s', $row['last_battle_time']), ':la'=>  date('Y-m-d H:i:s',$row['logout_at']), ':id' => $id);                
                $this->stmUpdateData->execute($uppp);
                
                $this->upd_counter ++;                
                $uppp = 0;
            }            
            $res = FALSE; $update = FALSE;
            
        }
        
    }
   
}

echo "\rSkript sa zacal vykonavat:  \t \t".date('Y-m-d H:i:s', time()). "\r";

$database = new Database();
$account = new account2();

$players = $database->SelectAccountId();

$i =  0;
$text_ids = "";

foreach($players as $id) {

    $text_ids .= $id['account_id'].",";
    $i ++;

    if($i == 100) {

      $data =  $account->players_info($text_ids);
      $database->ChceckNewData($data);

      $i = 0;
      $text_ids = NULL;
     
      
    }
}


echo "Skript skoncil : \t \t \t".date('Y-m-d H:i:s', time())."\r";
