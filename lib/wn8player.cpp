#include "./wn8player.h"


        wn8player::wn8player()
        {
            this->expTable();
        }

        

        float wn8player::GetWN8(int *PlayerData, mymap *TankData)
        {
            float wn8;
            
            wn8  = this->Calculate(PlayerData,TankData);
            return wn8;
        }
       
        float wn8player::Calculate(int *PlayerData, mymap *TankData)
        {   
            double expDMG,expSpot,expFrag,expDef,expWin;
            etv_data tmp;
            int tank_id,battles;

            for (auto& x: *TankData)
            {
                tank_id = x.first;   
                battles = x.second;
                
                tmp = this->etv[tank_id];
                
                expDMG      += tmp.dmg  * (double)battles;
                expSpot     += tmp.spot * (double)battles;
                expFrag     += tmp.frag * (double)battles;
                expDef      += tmp.def  * (double)battles;
                expWin      += 0.01 * tmp.win * (double)battles;

                
            }

            double rDMG,rSpot,rFrag,rDef,rWin;
            rDMG    = PlayerData[0]  / expDMG;
            rSpot   = PlayerData[1]  / expSpot;
            rFrag   = PlayerData[2]  / expFrag;
            rDef    = PlayerData[3]  / expDef;
            rWin    = PlayerData[4]  / expWin;

            double rDMGc,rSpotc,rFragc,rDefc,rWinc;

            

            rDMGc   = max(0.00,                    (rDMG - 0.22) / (1.00 - 0.22));
            rSpotc  = max(0.00, min(rDMGc + 0.1,   (rSpot - 0.38) / (1.00 - 0.38)));
            rFragc  = max(0.00, min(rDMGc + 0.2,   (rFrag - 0.12) / (1.00 - 0.12)));
            rDefc   = max(0.00, min(rDMGc + 0.1,   (rDef -  0.10) / (1.00 - 0.10)));
            rWinc   = max(0.00,                    (rWin - 0.71) / (1 - 0.71));


            float wn8 = 980 * rDMGc + 210 * rDMGc * rFragc + 155 * rFragc * rSpotc + 75 * rDefc * rFragc + 145 * min(1.8,rWinc);


            return wn8;

        }

        void wn8player::expTable()
        {
            PGresult *result;
            PGconn *conn;
            Pgsql *pgsql    = new Pgsql;
            conn = pgsql->Get();
            delete pgsql;

            const char query[] = "SELECT * FROM expected_tank_value"; 

            result = PQexec(conn,query);
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
                 {cout << "Chyba v select etv table: " <<  PQresultErrorMessage(result) << endl;}

            int ntuples = PQntuples(result);int tank_id;int i;
            for(i = 0; i < ntuples; i++)
            {
                tank_id    = stoi(PQgetvalue(result,i,0));

                data.frag  = stof(PQgetvalue(result,i,1));
                data.dmg   = stof(PQgetvalue(result,i,2));
                data.spot  = stof(PQgetvalue(result,i,3));
                data.def   = stof(PQgetvalue(result,i,4));
                data.win   = stof(PQgetvalue(result,i,5));

                this->etv[tank_id] = data;

            }

            PQclear(result);
            PQfinish(conn);

        }