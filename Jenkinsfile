// Global variable initialization
buildNumber = currentBuild.id
version = null
release_version = ""
branchName = ""
commitId = null
repoName = null
changelog = ""

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
       stage('Checkout source') {
           agent any
	   steps {
	       script {
	           def vcs = checkout([
		       $class: 'GitSCM',
                       changelog: true,
                       userRemoteConfigs: scm.userRemoteConfigs,
                       branches: scm.branches,
                       extensions: scm.extensions + [
                           [$class: 'SubmoduleOption', disableSubmodules: false, recursiveSubmodules: true, parentCredentials: true]
                       ]
		   ])
                   branchName = vcs.GIT_BRANCH
                   commitId = "${vcs.GIT_COMMIT}"[0..6]
                   repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                   release_version = branchName.replaceAll("[^\\d\\.]", "")
                   if (release_version.length() > 0 || branchName.contains('release')) {
                       version = release_version + "." + buildNumber
                   } else {
                       version = "0.0.${buildNumber}"
                   }
       	       }		   
       	   }
       }
       stage('Test + Build') {
            parallel {
                stage('Linux') {
                    agent {
			  dockerfile {
                            filename 'Dockerfile'
                            // Explicitly force the path to look inside common cargo locations
                            //args '-v /home/jenkins/workspace:/workspace --env PATH=/usr/share/cargo/bin:/build/avs/.cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'
			    additionalBuildArgs '--no-cache'
                        }
                    }
                    steps {
		    	sh '''
			  echo "Looking for cargo"
                          # Debug step to find where apt put cargo
                          whereis cargo || true
			  echo "CLANG:"
			  clang++ --version
                        '''
                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'			

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'
                        // run slow tests
                        sh './ztest-slow'

                        // cleanup old artifacts
                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'

                        // build
                        sh 'make dist_clean'
                        sh 'make zcall sectest AVS_VERSION=' + version
                        script {
                            def exitStatus = sh returnStatus: true, script: './sectest https://sft.calling-staging-v01.zinfra.io:443 > ./build/artifacts/avs-' + version + '-sectest.log'
                            if (exitStatus != 0) {
                                sh 'cat ./build/artifacts/avs-' + version + '-sectest.log'
                                error('sectest failed')
                            }
                        }
                        sh '''#!/bin/bash
                            . ./scripts/android_devenv.sh && make dist_linux dist_android AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
                        '''
                        sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
                        sh 'zip -9j ./build/artifacts/avs.android.' + version + '.zip ./build/dist/android/avs.aar'
                        sh 'zip -9j ./build/artifacts/zcall_linux_' + version + '.zip ./zcall'
                        sh 'if [ -e ./build/dist/android/debug/ ]; then cd ./build/dist/android/debug; zip -9r ./../../../artifacts/avs.android.' + version + '.debug.zip *; cd -; fi'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false

                        // Stash the android aar directory recursively,
                        // shared libraries will be used to generate android kmp in macos agent
                        stash name: 'android-aar', includes: 'build/dist/android/aar/**'
                    }
                }
            }
        }
        stage('Prepare changelog') {
            steps {
                script {
                    currentBuild.changeSets.each { set ->
                        set.items.each { entry ->
                            changelog += '- ' + entry.msg + '\n'
                        }
                    }
                }
                echo("Changelog:")
                echo(changelog)
            }
        }
        stage('Tag + Create Github release') {
            agent {
                label "linuxbuild"
            }
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }

            steps {
                echo "Tag as ${version}"
                withCredentials([sshUserPrivateKey(credentialsId: 'wire-avs', keyFileVariable: 'sshPrivateKeyPath')]) {
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"

                            git tag ${version}

                            git \
                                -c core.sshCommand='ssh -i ${sshPrivateKeyPath}' \
                                push \
                                origin ${version}
                        """
                    )
                }
                echo 'Creating release on Github'
                // Unfortunately it is not allowed to access github API with a deploy key so we have to rely on
                // a user token to upload via python github package
                withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                    string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                    // NOTE: creating an empty stub directory just to create the release
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"
                            GITHUB_USER=${repoUser} \
                            GITHUB_TOKEN=${accessToken} \
			    python3 ./scripts/release-on-github.py \
                                ${repoName} \
                                \$(mktemp -d) \
                                ${version} \
                                "${changelog}"
                        """
                    )
                }
            }
        }
        stage('Upload to Github') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }
            matrix {
                axes {
                    axis {
                        name 'AGENT'
                        values 'macos', 'linuxbuild'
                    }
                }
                agent {
                    label "${AGENT}"
                }

                stages {
                    stage( 'Uploading release artifacts' ) {
                        steps {
                            withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                                string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                                sh(
                                    script: """
                                        sleep 5
                                        GITHUB_USER=${repoUser} \
                                        GITHUB_TOKEN=${accessToken} \
                                        python3 ./scripts/release-on-github.py \
                                            ${repoName} \
                                            ./build/artifacts \
                                            ${version} \
                                            "${changelog}"
                                    """
                                )
                            }
                        }
                    }
                }
            }
        }
        stage('Publish to sonatype') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            environment {
                PATH = "/opt/homebrew/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${ env.PATH }"
            }
            steps {
                script {
                    echo '### Sign and upload to sonatype'
                    withCredentials([
                            usernamePassword( credentialsId: 'sonatype-central', usernameVariable: 'ORG_GRADLE_PROJECT_mavenCentralUsername', passwordVariable: 'ORG_GRADLE_PROJECT_mavenCentralPassword' ),
                            string(credentialsId: 'sonatype-signing-key-password', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKeyPassword'),
                            string(credentialsId: 'sonatype-signing-key', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKey')
                        ]) {
                        withMaven(maven: 'M3', jdk: 'JDK17') {
                            sh(
                                script: """
                                    touch local.properties
                                    ORG_GRADLE_PROJECT_VERSION_NAME=$version ./gradlew :publishAndReleaseToMavenCentral
                                """
                            )
                        }
                    }
                }
            }
        }

        // WPB-24450 macos agent is handling kmp publish
        // When migration is complate, 'Publish to sonatype' legacy android step can be removed.
        stage('Publish kmp to sonatype') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'macos'
            }
            environment {
                PATH = "/opt/homebrew/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${ env.PATH }"
            }
            steps {
                // Restore the android shared libraries generated in linux agent
                unstash 'android-aar'

                script {
                    echo '### Sign and upload to sonatype'
                    withCredentials([
                            usernamePassword( credentialsId: 'sonatype-central', usernameVariable: 'ORG_GRADLE_PROJECT_mavenCentralUsername', passwordVariable: 'ORG_GRADLE_PROJECT_mavenCentralPassword' ),
                            string(credentialsId: 'sonatype-signing-key-password', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKeyPassword'),
                            string(credentialsId: 'sonatype-signing-key', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKey')
                        ]) {
                        withMaven(maven: 'M3', jdk: 'JDK17') {
                            sh(
                                script: """
                                    ORG_GRADLE_PROJECT_VERSION_NAME=$version ./gradlew avs:clean
                                    ORG_GRADLE_PROJECT_VERSION_NAME=$version ./gradlew :avs:publishAndReleaseToMavenCentral --no-configuration-cache
                                """
                            )
                        }
                    }
                }
            }
        }

//        stage('Publish to ios github repo') {
//            when {
//                anyOf {
//                    expression { return "${branchName}".contains('release') }
//                }
//            }
//            agent {
//                label 'built-in'
//            }
//            steps {
//                withCredentials([ string( credentialsId: 'ios-github', variable: 'accessToken' ) ]) {
//                    sh """
//                        GITHUB_TOKEN=${ accessToken } \
//                        python3 ./scripts/upload-ios.py \
//                            ./build/artifacts/avs.xcframework.zip \
//                            ${version} \
//                            appstore
//                    """
//                }
//            }
//        }
    }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
                }
            }
        }

        success {
            node( 'macos' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "✅ ${JOB_NAME} #${BUILD_ID} succeeded\n**Changelog:**\n${changelog}\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'macos' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "❌ ${JOB_NAME} #${BUILD_ID} failed\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }
    }
}
